/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "document_chunker.h"
#include "core.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace {

std::string trim_copy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

size_t count_lines(const std::string& text) {
    if (text.empty()) {
        return 0;
    }
    return static_cast<size_t>(std::count(text.begin(), text.end(), '\n') + 1);
}

bool is_markdown_document(const Document& document) {
    return document.type == "markdown" ||
           (document.metadata.find("mime_type") != document.metadata.end() &&
               document.metadata.at("mime_type") == "text/markdown") ||
           document.relative_path.ends_with(".md") ||
           document.relative_path.ends_with(".markdown") ||
           document.relative_path.ends_with(".rst") ||
           document.relative_path.ends_with(".adoc");
}

bool is_code_document(const Document& document) {
    return document.type == "source" ||
           document.language == "cpp" ||
           document.language == "c" ||
           document.language == "python" ||
           document.language == "javascript" ||
           document.language == "typescript" ||
           document.language == "java" ||
           document.language == "go" ||
           document.language == "rust";
}

bool is_pdf_document(const Document& document) {
    return document.type == "pdf" ||
           (document.metadata.find("mime_type") != document.metadata.end() &&
               document.metadata.at("mime_type") == "application/pdf");
}

bool is_spreadsheet_document(const Document& document) {
    return document.type == "csv" ||
           document.type == "xlsx" ||
           document.relative_path.ends_with(".csv") ||
           document.relative_path.ends_with(".xlsx");
}

bool is_image_ocr_document(const Document& document) {
    const auto parser = document.metadata.find("ingestion_parser");
    return document.type == "image" ||
           (parser != document.metadata.end() && parser->second == "image_tesseract_ocr");
}

std::string metadata_value(const Document& document, const std::string& key) {
    const auto it = document.metadata.find(key);
    return it == document.metadata.end() ? "" : it->second;
}

size_t parse_size_or_zero(const std::string& value) {
    if (value.empty()) {
        return 0;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    return end == value.c_str() ? 0 : static_cast<size_t>(parsed);
}

struct SymbolInfo {
    bool found = false;
    std::string kind;
    std::string name;
    std::string display;
};

SymbolInfo detect_symbol(const std::string& line, const std::string& language) {
    const std::vector<std::pair<std::regex, std::pair<std::string, size_t>>> patterns = {
        {std::regex(R"(^\s*class\s+([A-Za-z_]\w*))"), {"class", 1}},
        {std::regex(R"(^\s*struct\s+([A-Za-z_]\w*))"), {"struct", 1}},
        {std::regex(R"(^\s*enum\s+(?:class\s+)?([A-Za-z_]\w*))"), {"enum", 1}},
        {std::regex(R"(^\s*def\s+([A-Za-z_]\w*)\s*\()"), {"function", 1}},
        {std::regex(R"(^\s*async\s+def\s+([A-Za-z_]\w*)\s*\()"), {"function", 1}},
        {std::regex(R"(^\s*(?:export\s+)?(?:async\s+)?function\s+([A-Za-z_$][\w$]*)\s*\()"), {"function", 1}},
        {std::regex(R"(^\s*(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*=\s*(?:async\s*)?\([^)]*\)\s*=>)"), {"function", 1}},
        {std::regex(R"(^\s*func\s+(?:\([^)]+\)\s*)?([A-Za-z_]\w*)\s*\()"), {"function", 1}},
        {std::regex(R"(^\s*fn\s+([A-Za-z_]\w*)\s*\()"), {"function", 1}},
        {std::regex(R"(^\s*impl\s+([A-Za-z_]\w*))"), {"impl", 1}},
        {std::regex(R"(^\s*(?:public|private|protected|static|virtual|inline|constexpr|friend|\s)*[\w:<>\*&]+\s+([A-Za-z_~]\w*)\s*\([^;]*\)\s*(?:const\s*)?(?:\{|$))"), {"function", 1}}
    };

    (void)language;
    for (const auto& [pattern, descriptor] : patterns) {
        std::smatch match;
        if (std::regex_search(line, match, pattern) && match.size() > descriptor.second) {
            SymbolInfo info;
            info.found = true;
            info.kind = descriptor.first;
            info.name = match[descriptor.second].str();
            info.display = trim_copy(line);
            if (info.display.size() > 120) {
                info.display.resize(120);
            }
            return info;
        }
    }
    return {};
}

} // namespace

DocumentChunker::DocumentChunker()
    : DocumentChunker(Config{}) {
}

DocumentChunker::DocumentChunker(Config config)
    : config_(std::move(config)) {
    if (config_.max_tokens == 0) {
        config_.max_tokens = 220;
    }
    if (config_.overlap_tokens >= config_.max_tokens) {
        config_.overlap_tokens = config_.max_tokens / 5;
    }
    if (config_.tokenizer_max_tokens > 0 && config_.max_tokens > config_.tokenizer_max_tokens) {
        config_.max_tokens = config_.tokenizer_max_tokens;
    }
}

std::vector<Document> DocumentChunker::chunkDocument(const Document& document) const {
    if (document.content.empty()) {
        return {};
    }

    const std::string strategy = strategyFor(document);
    if (strategy == "markdown_heading") {
        return chunkSections(document, splitMarkdownSections(document), strategy);
    }
    if (strategy == "code_symbol") {
        return chunkSections(document, splitCodeSections(document), strategy);
    }
    if (strategy == "pdf_page") {
        return chunkSections(document, splitPdfPageSections(document), strategy);
    }
    if (strategy == "spreadsheet_table") {
        return chunkSections(document, splitSpreadsheetSections(document), strategy);
    }
    if (strategy == "image_ocr_region") {
        return chunkSections(document, splitOcrSections(document), strategy);
    }

    Section section;
    section.text = document.content;
    section.char_start = 0;
    return chunkSections(document, {section}, strategy);
}

std::vector<DocumentChunker::Section> DocumentChunker::splitMarkdownSections(const Document& document) const {
    std::vector<Section> sections;
    std::istringstream stream(document.content);
    std::string line;
    std::string current;
    std::string current_heading;
    size_t current_start = 0;
    size_t offset = 0;
    bool has_current = false;

    const std::regex heading_re(R"(^\s{0,3}(#{1,6})\s+(.+?)\s*$)");
    while (std::getline(stream, line)) {
        std::smatch match;
        const bool heading = std::regex_match(line, match, heading_re);
        if (heading && has_current && !trim_copy(current).empty()) {
            Section section;
            section.text = current;
            section.char_start = current_start;
            section.heading = current_heading;
            sections.push_back(std::move(section));
            current.clear();
            current_start = offset;
        } else if (!has_current) {
            current_start = offset;
            has_current = true;
        }

        if (heading) {
            current_heading = trim_copy(match[2].str());
        }

        current += line;
        current += '\n';
        offset += line.size() + 1;
    }

    if (!trim_copy(current).empty()) {
        Section section;
        section.text = current;
        section.char_start = current_start;
        section.heading = current_heading;
        sections.push_back(std::move(section));
    }

    if (sections.empty()) {
        Section section;
        section.text = document.content;
        section.char_start = 0;
        sections.push_back(std::move(section));
    }
    return sections;
}

std::vector<DocumentChunker::Section> DocumentChunker::splitCodeSections(const Document& document) const {
    std::vector<Section> sections;
    std::istringstream stream(document.content);
    std::string line;
    std::string current;
    std::string current_symbol;
    size_t current_start = 0;
    size_t offset = 0;
    bool has_current = false;

    while (std::getline(stream, line)) {
        const auto symbol = detect_symbol(line, document.language);
        if (symbol.found && has_current && !trim_copy(current).empty()) {
            Section section;
            section.text = current;
            section.char_start = current_start;
            section.symbol = current_symbol;
            sections.push_back(section);
            current.clear();
            current_start = offset;
        } else if (!has_current) {
            current_start = offset;
            has_current = true;
        }

        if (symbol.found) {
            current_symbol = symbol.display;
        }

        current += line;
        current += '\n';
        offset += line.size() + 1;
    }

    if (!trim_copy(current).empty()) {
        Section section;
        section.text = current;
        section.char_start = current_start;
        section.symbol = current_symbol;
        const auto symbol = detect_symbol(current_symbol, document.language);
        if (symbol.found) {
            section.symbol_name = symbol.name;
            section.symbol_kind = symbol.kind;
        }
        sections.push_back(section);
    }

    if (sections.empty()) {
        Section section;
        section.text = document.content;
        section.char_start = 0;
        sections.push_back(std::move(section));
    } else {
        for (auto& section : sections) {
            if (!section.symbol.empty() && section.symbol_name.empty()) {
                const auto symbol = detect_symbol(section.symbol, document.language);
                if (symbol.found) {
                    section.symbol_name = symbol.name;
                    section.symbol_kind = symbol.kind;
                }
            }
        }
    }
    return sections;
}

std::vector<DocumentChunker::Section> DocumentChunker::splitPdfPageSections(const Document& document) const {
    std::vector<Section> sections;
    size_t page = 1;
    size_t start = 0;
    size_t pos = 0;
    while (pos <= document.content.size()) {
        if (pos == document.content.size() || document.content[pos] == '\f') {
            std::string text = document.content.substr(start, pos - start);
            text = trim_copy(std::regex_replace(text, std::regex(R"(^\s*Page\s+\d+:\s*)"), ""));
            if (!text.empty()) {
                Section section;
                section.text = text;
                section.char_start = start;
                section.page_number = page;
                sections.push_back(std::move(section));
            }
            ++page;
            start = pos + 1;
        }
        ++pos;
    }
    if (sections.empty()) {
        Section section;
        section.text = document.content;
        section.char_start = 0;
        section.page_number = parse_size_or_zero(metadata_value(document, "pdf_page_start"));
        if (section.page_number == 0) {
            section.page_number = 1;
        }
        sections.push_back(std::move(section));
    }
    return sections;
}

std::vector<DocumentChunker::Section> DocumentChunker::splitSpreadsheetSections(const Document& document) const {
    std::vector<Section> sections;
    std::istringstream stream(document.content);
    std::string line;
    std::string current_sheet = document.type == "csv" ? "CSV" : "";
    size_t offset = 0;

    while (std::getline(stream, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.rfind("Sheet: ", 0) == 0) {
            current_sheet = trimmed.substr(7);
            offset += line.size() + 1;
            continue;
        }

        std::smatch row_match;
        if (std::regex_match(trimmed, row_match, std::regex(R"(^Row\s+(\d+):\s*(.*)$)")) && row_match.size() > 2) {
            Section section;
            section.text = trimmed;
            section.char_start = offset;
            section.sheet_name = current_sheet.empty() ? "Sheet" : current_sheet;
            section.row_start = parse_size_or_zero(row_match[1].str());
            section.row_end = section.row_start;
            sections.push_back(std::move(section));
        }
        offset += line.size() + 1;
    }

    if (sections.empty()) {
        Section section;
        section.text = document.content;
        section.char_start = 0;
        sections.push_back(std::move(section));
    }
    return sections;
}

std::vector<DocumentChunker::Section> DocumentChunker::splitOcrSections(const Document& document) const {
    Section section;
    section.text = document.content;
    section.char_start = 0;
    section.ocr_region = metadata_value(document, "ocr_region_0");
    if (section.ocr_region.empty()) {
        section.ocr_region = "full_image";
    }
    section.ocr_caption = metadata_value(document, "ocr_caption");
    return {section};
}

std::vector<Document> DocumentChunker::chunkSections(const Document& document,
                                                     const std::vector<Section>& sections,
                                                     const std::string& strategy) const {
    std::vector<Document> chunks;
    const size_t max_tokens = effectiveMaxTokens(document);
    const size_t overlap_tokens = std::min(config_.overlap_tokens, max_tokens > 0 ? max_tokens - 1 : 0);
    const size_t step = max_tokens > overlap_tokens
        ? max_tokens - overlap_tokens
        : max_tokens;

    for (const auto& section : sections) {
        const auto spans = tokenSpans(section.text);
        if (spans.empty()) {
            continue;
        }

        for (size_t start_token = 0; start_token < spans.size();) {
            const size_t end_token = std::min(spans.size(), start_token + max_tokens);
            const size_t char_start = spans[start_token].start;
            const size_t char_end = spans[end_token - 1].end;
            std::string content = trim_copy(section.text.substr(char_start, char_end - char_start));
            if (!content.empty()) {
                Document chunk = document;
                const size_t chunk_index = chunks.size();
                chunk.content = content;
                chunk.relative_path = makeChunkRelativePath(document, chunk_index);
                chunk.size_bytes = content.size();
                chunk.lines_count = count_lines(content);
                chunk.hash = HashCalculator::compute_md5(content);
                chunk.embedding.clear();
                chunk.metadata["chunk_of"] = document.relative_path.empty() ? document.path : document.relative_path;
                chunk.metadata["chunk_index"] = std::to_string(chunk_index);
                chunk.metadata["chunk_strategy"] = strategy;
                chunk.metadata["chunk_char_start"] = std::to_string(section.char_start + char_start);
                chunk.metadata["chunk_char_end"] = std::to_string(section.char_start + char_end);
                chunk.metadata["chunk_token_count"] = std::to_string(end_token - start_token);
                chunk.metadata["chunk_token_budget"] = std::to_string(max_tokens);
                if (!section.heading.empty()) {
                    chunk.metadata["chunk_heading"] = section.heading;
                }
                if (!section.symbol.empty()) {
                    chunk.metadata["chunk_symbol"] = section.symbol;
                }
                if (!section.symbol_name.empty()) {
                    chunk.metadata["chunk_symbol_name"] = section.symbol_name;
                }
                if (!section.symbol_kind.empty()) {
                    chunk.metadata["chunk_symbol_kind"] = section.symbol_kind;
                }
                if (section.page_number > 0) {
                    chunk.metadata["chunk_page"] = std::to_string(section.page_number);
                    chunk.metadata["chunk_page_start"] = std::to_string(section.page_number);
                    chunk.metadata["chunk_page_end"] = std::to_string(section.page_number);
                }
                if (!section.sheet_name.empty()) {
                    chunk.metadata["chunk_sheet"] = section.sheet_name;
                }
                if (section.row_start > 0) {
                    chunk.metadata["chunk_row_start"] = std::to_string(section.row_start);
                    chunk.metadata["chunk_row_end"] = std::to_string(section.row_end > 0 ? section.row_end : section.row_start);
                }
                if (!section.ocr_region.empty()) {
                    chunk.metadata["chunk_ocr_region"] = section.ocr_region;
                }
                if (!section.ocr_caption.empty()) {
                    chunk.metadata["chunk_ocr_caption"] = section.ocr_caption;
                }
                chunks.push_back(std::move(chunk));
            }

            if (end_token == spans.size()) {
                break;
            }
            start_token += step;
        }
    }

    const size_t chunk_count = chunks.size();
    for (auto& chunk : chunks) {
        chunk.metadata["chunk_count"] = std::to_string(chunk_count);
    }
    return chunks;
}

std::vector<DocumentChunker::TokenSpan> DocumentChunker::tokenSpans(const std::string& text) const {
    std::vector<TokenSpan> spans;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        const size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        spans.push_back({start, i});
    }
    return spans;
}

size_t DocumentChunker::effectiveMaxTokens(const Document& document) const {
    size_t max_tokens = config_.max_tokens == 0 ? 220 : config_.max_tokens;
    const size_t config_limit = config_.tokenizer_max_tokens;
    const size_t metadata_limit = parse_size_or_zero(metadata_value(document, "tokenizer_max_tokens"));
    const size_t embedding_limit = parse_size_or_zero(metadata_value(document, "embedding_token_limit"));
    for (const size_t limit : {config_limit, metadata_limit, embedding_limit}) {
        if (limit > 0) {
            max_tokens = std::min(max_tokens, limit);
        }
    }
    return std::max<size_t>(1, max_tokens);
}

std::string DocumentChunker::strategyFor(const Document& document) const {
    if (is_pdf_document(document)) {
        return "pdf_page";
    }
    if (is_spreadsheet_document(document)) {
        return "spreadsheet_table";
    }
    if (is_image_ocr_document(document)) {
        return "image_ocr_region";
    }
    if (is_markdown_document(document)) {
        return "markdown_heading";
    }
    if (is_code_document(document)) {
        return "code_symbol";
    }
    return "plain_text_token";
}

std::string DocumentChunker::makeChunkRelativePath(const Document& document, size_t chunk_index) const {
    const std::string base = document.relative_path.empty() ? document.path : document.relative_path;
    return base + "#chunk-" + std::to_string(chunk_index);
}
