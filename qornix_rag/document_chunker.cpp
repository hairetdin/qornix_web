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
            sections.push_back({current, current_start, current_heading, ""});
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
        sections.push_back({current, current_start, current_heading, ""});
    }

    if (sections.empty()) {
        sections.push_back({document.content, 0, "", ""});
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

    const std::regex symbol_re(
        R"(^\s*(?:template\s*<[^>]+>\s*)?(?:[\w:<>\*&]+\s+)+(?:[\w:~]+\s*)\([^;]*\)\s*(?:const\s*)?(?:\{|$)|^\s*(?:class|struct|enum)\s+([A-Za-z_]\w*))"
    );

    while (std::getline(stream, line)) {
        std::smatch match;
        const bool symbol = std::regex_search(line, match, symbol_re);
        if (symbol && has_current && !trim_copy(current).empty()) {
            sections.push_back({current, current_start, "", current_symbol});
            current.clear();
            current_start = offset;
        } else if (!has_current) {
            current_start = offset;
            has_current = true;
        }

        if (symbol) {
            current_symbol = trim_copy(line);
            if (current_symbol.size() > 120) {
                current_symbol.resize(120);
            }
        }

        current += line;
        current += '\n';
        offset += line.size() + 1;
    }

    if (!trim_copy(current).empty()) {
        sections.push_back({current, current_start, "", current_symbol});
    }

    if (sections.empty()) {
        sections.push_back({document.content, 0, "", ""});
    }
    return sections;
}

std::vector<Document> DocumentChunker::chunkSections(const Document& document,
                                                     const std::vector<Section>& sections,
                                                     const std::string& strategy) const {
    std::vector<Document> chunks;
    const size_t step = config_.max_tokens > config_.overlap_tokens
        ? config_.max_tokens - config_.overlap_tokens
        : config_.max_tokens;

    for (const auto& section : sections) {
        const auto spans = tokenSpans(section.text);
        if (spans.empty()) {
            continue;
        }

        for (size_t start_token = 0; start_token < spans.size();) {
            const size_t end_token = std::min(spans.size(), start_token + config_.max_tokens);
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
                if (!section.heading.empty()) {
                    chunk.metadata["chunk_heading"] = section.heading;
                }
                if (!section.symbol.empty()) {
                    chunk.metadata["chunk_symbol"] = section.symbol;
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

std::string DocumentChunker::strategyFor(const Document& document) const {
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
