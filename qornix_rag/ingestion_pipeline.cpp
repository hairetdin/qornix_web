/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "ingestion_pipeline.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

#ifndef QORNIX_HAS_LIBZIP
#define QORNIX_HAS_LIBZIP 0
#endif

#ifndef QORNIX_HAS_XLSX
#define QORNIX_HAS_XLSX 0
#endif

#ifndef QORNIX_HAS_OPENXML
#define QORNIX_HAS_OPENXML 0
#endif

#if QORNIX_HAS_LIBZIP
#include <zip.h>
#endif

#if QORNIX_HAS_XLSX || QORNIX_HAS_OPENXML
#include <pugixml.hpp>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace qornix::rag {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string hashContent(const std::string& data) {
    unsigned long hash = 5381;
    for (char c : data) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
    }
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

std::chrono::system_clock::time_point fileTimeToSystem(fs::file_time_type file_time) {
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
}

bool looksBinary(const std::string& content) {
    const auto sample_size = std::min<size_t>(content.size(), 4096);
    for (size_t i = 0; i < sample_size; ++i) {
        const unsigned char ch = static_cast<unsigned char>(content[i]);
        if (ch == 0) {
            return true;
        }
    }
    return false;
}

std::string collapseWhitespace(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    bool previous_space = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            if (!previous_space) {
                output.push_back(' ');
            }
            previous_space = true;
        } else {
            output.push_back(static_cast<char>(ch));
            previous_space = false;
        }
    }
    if (!output.empty() && output.front() == ' ') {
        output.erase(output.begin());
    }
    if (!output.empty() && output.back() == ' ') {
        output.pop_back();
    }
    return output;
}

std::string trimWhitespace(const std::string& value) {
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

std::string normalizeExtractedTextPreservingLines(const std::string& value) {
    std::stringstream input(value);
    std::stringstream output;
    std::string line;
    bool previous_blank = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto trimmed = trimWhitespace(line);
        if (trimmed.empty()) {
            if (!previous_blank) {
                output << '\n';
            }
            previous_blank = true;
        } else {
            output << trimmed << '\n';
            previous_blank = false;
        }
    }
    return trimWhitespace(output.str());
}

std::string normalizePdfTextPreservingPages(const std::string& value, size_t& page_count) {
    std::stringstream output;
    size_t start = 0;
    size_t pos = 0;
    size_t page = 1;
    page_count = 0;
    while (pos <= value.size()) {
        if (pos == value.size() || value[pos] == '\f') {
            const auto page_text = normalizeExtractedTextPreservingLines(value.substr(start, pos - start));
            if (!page_text.empty()) {
                if (page_count > 0) {
                    output << "\f\n";
                }
                output << "Page " << page << ":\n" << page_text << "\n";
                ++page_count;
            }
            ++page;
            start = pos + 1;
        }
        ++pos;
    }
    return trimWhitespace(output.str());
}

std::string htmlEntityDecode(std::string value) {
    const std::pair<const char*, const char*> replacements[] = {
        {"&nbsp;", " "},
        {"&amp;", "&"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&quot;", "\""},
        {"&#39;", "'"}
    };
    for (const auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = value.find(from, pos)) != std::string::npos) {
            value.replace(pos, std::strlen(from), to);
            pos += std::strlen(to);
        }
    }
    return value;
}

std::string stripHtmlToText(const std::string& html) {
    std::string cleaned = std::regex_replace(html, std::regex("(?is)<script[^>]*>.*?</script>"), " ");
    cleaned = std::regex_replace(cleaned, std::regex("(?is)<style[^>]*>.*?</style>"), " ");
    cleaned = std::regex_replace(cleaned, std::regex("(?is)<(br|p|div|section|article|header|footer|li|tr|h[1-6])[^>]*>"), "\n");
    cleaned = std::regex_replace(cleaned, std::regex("(?is)<[^>]+>"), " ");
    return collapseWhitespace(htmlEntityDecode(cleaned));
}

std::string extractHtmlTitle(const std::string& html) {
    std::smatch match;
    if (std::regex_search(html, match, std::regex("(?is)<title[^>]*>(.*?)</title>")) && match.size() > 1) {
        return collapseWhitespace(htmlEntityDecode(match[1].str()));
    }
    return {};
}

const std::set<std::string>& sourceExtensions() {
    static const std::set<std::string> extensions = {
        ".cpp", ".c", ".cc", ".cxx", ".c++",
        ".h", ".hpp", ".hxx", ".h++",
        ".java", ".cs", ".py", ".js", ".ts",
        ".go", ".rs", ".swift", ".kt", ".scala"
    };
    return extensions;
}

const std::map<std::string, std::pair<std::string, std::string>>& textExtensionMap() {
    static const std::map<std::string, std::pair<std::string, std::string>> mapping = {
        {".cpp", {"text/x-c++src", "C++"}},
        {".c", {"text/x-csrc", "C"}},
        {".cc", {"text/x-c++src", "C++"}},
        {".cxx", {"text/x-c++src", "C++"}},
        {".c++", {"text/x-c++src", "C++"}},
        {".h", {"text/x-chdr", "C/C++ Header"}},
        {".hpp", {"text/x-c++hdr", "C++ Header"}},
        {".hxx", {"text/x-c++hdr", "C++ Header"}},
        {".h++", {"text/x-c++hdr", "C++ Header"}},
        {".java", {"text/x-java-source", "Java"}},
        {".cs", {"text/x-csharp", "C#"}},
        {".py", {"text/x-python", "Python"}},
        {".js", {"text/javascript", "JavaScript"}},
        {".ts", {"text/typescript", "TypeScript"}},
        {".go", {"text/x-go", "Go"}},
        {".rs", {"text/x-rust", "Rust"}},
        {".swift", {"text/x-swift", "Swift"}},
        {".kt", {"text/x-kotlin", "Kotlin"}},
        {".scala", {"text/x-scala", "Scala"}},
        {".txt", {"text/plain", "Text"}},
        {".md", {"text/markdown", "Markdown"}},
        {".rst", {"text/x-rst", "reStructuredText"}},
        {".adoc", {"text/asciidoc", "AsciiDoc"}},
        {".yaml", {"application/x-yaml", "YAML"}},
        {".yml", {"application/x-yaml", "YAML"}},
        {".json", {"application/json", "JSON"}},
        {".xml", {"application/xml", "XML"}},
        {".html", {"text/html", "HTML"}},
        {".htm", {"text/html", "HTML"}},
        {".pdf", {"application/pdf", "PDF"}},
        {".docx", {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", "DOCX"}},
        {".csv", {"text/csv", "CSV"}},
        {".xlsx", {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "XLSX"}},
        {".pptx", {"application/vnd.openxmlformats-officedocument.presentationml.presentation", "PPTX"}},
        {".png", {"image/png", "Image"}},
        {".jpg", {"image/jpeg", "Image"}},
        {".jpeg", {"image/jpeg", "Image"}},
        {".tif", {"image/tiff", "Image"}},
        {".tiff", {"image/tiff", "Image"}},
        {".bmp", {"image/bmp", "Image"}},
        {".webp", {"image/webp", "Image"}},
        {".pbm", {"image/x-portable-bitmap", "Image"}},
        {".pgm", {"image/x-portable-graymap", "Image"}},
        {".ppm", {"image/x-portable-pixmap", "Image"}},
        {".pnm", {"image/x-portable-anymap", "Image"}},
        {".toml", {"application/toml", "TOML"}},
        {".ini", {"text/plain", "INI"}},
        {".cfg", {"text/plain", "Config"}},
        {".conf", {"text/plain", "Config"}},
        {".cmake", {"text/x-cmake", "CMake"}}
    };
    return mapping;
}

std::vector<std::string> defaultExtensions() {
    std::vector<std::string> extensions;
    extensions.reserve(textExtensionMap().size());
    for (const auto& [extension, _] : textExtensionMap()) {
        extensions.push_back(extension);
    }
    return extensions;
}

class PlainTextParser final : public DocumentParser {
public:
    std::string id() const override { return "plain_text"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.mime_type != "text/html";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }
        if (looksBinary(raw_content)) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "binary_content", "File appears to be binary"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = raw_content;
        doc.document_type = detected.document_type;
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = doc.content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        return doc;
    }
};

class HtmlParser final : public DocumentParser {
public:
    std::string id() const override { return "html"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.mime_type == "text/html";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }
        if (looksBinary(raw_content)) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "binary_content", "File appears to be binary"});
            return std::nullopt;
        }

        auto title = extractHtmlTitle(raw_content);
        auto body = stripHtmlToText(raw_content);
        if (body.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_html", "HTML produced no text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = title.empty() ? body : (title + "\n\n" + body);
        doc.document_type = "html";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        if (!title.empty()) {
            doc.metadata["title"] = title;
        }
        return doc;
    }
};

char detectCsvDelimiter(const std::string& content) {
    const std::vector<char> candidates = {',', ';', '\t', '|'};
    std::map<char, size_t> counts;
    bool in_quotes = false;
    for (char ch : content) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (!in_quotes && (ch == '\n' || ch == '\r')) {
            break;
        }
        if (!in_quotes) {
            for (char candidate : candidates) {
                if (ch == candidate) {
                    counts[candidate]++;
                }
            }
        }
    }

    char best = ',';
    size_t best_count = 0;
    for (char candidate : candidates) {
        const auto count = counts[candidate];
        if (count > best_count) {
            best = candidate;
            best_count = count;
        }
    }
    return best;
}

std::vector<std::vector<std::string>> parseCsvRows(const std::string& content, char delimiter) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < content.size(); ++i) {
        const char ch = content[i];
        if (in_quotes) {
            if (ch == '"') {
                if (i + 1 < content.size() && content[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(ch);
            }
            continue;
        }

        if (ch == '"') {
            in_quotes = true;
        } else if (ch == delimiter) {
            row.push_back(field);
            field.clear();
        } else if (ch == '\n' || ch == '\r') {
            if (ch == '\r' && i + 1 < content.size() && content[i + 1] == '\n') {
                ++i;
            }
            row.push_back(field);
            field.clear();
            rows.push_back(row);
            row.clear();
        } else {
            field.push_back(ch);
        }
    }

    if (!field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}

std::string joinFields(const std::vector<std::string>& fields, const std::string& separator) {
    std::string output;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            output += separator;
        }
        output += collapseWhitespace(fields[i]);
    }
    return output;
}

class CsvParser final : public DocumentParser {
public:
    std::string id() const override { return "csv"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.extension == ".csv";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }
        if (looksBinary(raw_content)) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "binary_content", "File appears to be binary"});
            return std::nullopt;
        }

        const char delimiter = detectCsvDelimiter(raw_content);
        const auto rows = parseCsvRows(raw_content, delimiter);
        if (rows.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_csv", "CSV produced no rows"});
            return std::nullopt;
        }

        size_t column_count = 0;
        for (const auto& row : rows) {
            column_count = std::max(column_count, row.size());
        }

        std::stringstream content;
        content << "CSV table: " << fs::path(path).filename().string() << "\n";
        content << "Columns: " << column_count << "\n";
        for (size_t i = 0; i < rows.size(); ++i) {
            content << "Row " << (i + 1) << ": " << joinFields(rows[i], " | ") << "\n";
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = content.str();
        doc.document_type = "csv";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["csv_delimiter"] = delimiter == '\t' ? "\\t" : std::string(1, delimiter);
        doc.metadata["csv_row_count"] = std::to_string(rows.size());
        doc.metadata["csv_column_count"] = std::to_string(column_count);
        if (!rows.empty()) {
            doc.metadata["csv_headers"] = joinFields(rows.front(), ",");
        }
        doc.metadata["structure_contract"] = "spreadsheet_rows_v1";
        doc.metadata["table_sheet_count"] = "1";
        doc.metadata["table_row_count"] = std::to_string(rows.size());
        doc.metadata["table_column_count"] = std::to_string(column_count);
        return doc;
    }
};

class PdfParser final : public DocumentParser {
public:
    std::string id() const override { return "pdf_pdftotext"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.mime_type == "application/pdf";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

        const auto extracted = extractText(path);
        if (!extracted.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, extracted.code, extracted.message});
            return std::nullopt;
        }

        size_t page_count = 0;
        const auto text = normalizePdfTextPreservingPages(extracted.text, page_count);
        if (text.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_pdf_text", "PDF produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = text;
        doc.document_type = "pdf";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["pdf_text_extractor"] = "pdftotext";
        doc.metadata["structure_contract"] = "pdf_pages_v1";
        doc.metadata["pdf_page_count"] = std::to_string(page_count);
        return doc;
    }

private:
    struct ExtractResult {
        bool ok = false;
        std::string text;
        std::string code;
        std::string message;
    };

    static ExtractResult extractText(const std::string& path) {
#if defined(__unix__) || defined(__APPLE__)
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            return {false, {}, "pdf_pipe_failed", "Could not create pipe for pdftotext"};
        }

        const pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return {false, {}, "pdf_fork_failed", "Could not start pdftotext"};
        }

        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            execlp("pdftotext",
                   "pdftotext",
                   "-layout",
                   "-enc",
                   "UTF-8",
                   path.c_str(),
                   "-",
                   static_cast<char*>(nullptr));
            _exit(127);
        }

        close(pipefd[1]);
        std::string output;
        char buffer[4096];
        ssize_t bytes = 0;
        while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            output.append(buffer, static_cast<size_t>(bytes));
        }
        close(pipefd[0]);

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            return {false, {}, "pdf_wait_failed", "Could not wait for pdftotext"};
        }
        if (!WIFEXITED(status)) {
            return {false, {}, "pdf_extractor_failed", "pdftotext terminated unexpectedly"};
        }

        const int exit_code = WEXITSTATUS(status);
        if (exit_code == 127) {
            return {false, {}, "pdf_extractor_unavailable", "pdftotext is not installed or not in PATH"};
        }
        if (exit_code != 0) {
            return {false, {}, "pdf_extract_failed", "pdftotext failed with exit code " + std::to_string(exit_code)};
        }
        return {true, output, {}, {}};
#else
        (void)path;
        return {false, {}, "pdf_extractor_unavailable", "PDF extraction requires pdftotext on this platform"};
#endif
    }
};

class ImageOcrParser final : public DocumentParser {
public:
    std::string id() const override { return "image_tesseract_ocr"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.mime_type.rfind("image/", 0) == 0;
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

        const auto extracted = extractText(path);
        if (!extracted.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, extracted.code, extracted.message});
            return std::nullopt;
        }

        const auto text = collapseWhitespace(extracted.text);
        if (text.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_ocr_text", "Image OCR produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = text;
        doc.document_type = "image";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["image_ocr_engine"] = "tesseract";
        doc.metadata["image_ocr_language"] = "default";
        doc.metadata["structure_contract"] = "ocr_regions_v1";
        doc.metadata["ocr_region_count"] = "1";
        doc.metadata["ocr_region_0"] = "full_image";
        return doc;
    }

private:
    struct ExtractResult {
        bool ok = false;
        std::string text;
        std::string code;
        std::string message;
    };

    static ExtractResult extractText(const std::string& path) {
#if defined(__unix__) || defined(__APPLE__)
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            return {false, {}, "image_ocr_pipe_failed", "Could not create pipe for tesseract"};
        }

        const pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return {false, {}, "image_ocr_fork_failed", "Could not start tesseract"};
        }

        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            execlp("tesseract",
                   "tesseract",
                   path.c_str(),
                   "stdout",
                   static_cast<char*>(nullptr));
            _exit(127);
        }

        close(pipefd[1]);
        std::string output;
        char buffer[4096];
        ssize_t bytes = 0;
        while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            output.append(buffer, static_cast<size_t>(bytes));
        }
        close(pipefd[0]);

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            return {false, {}, "image_ocr_wait_failed", "Could not wait for tesseract"};
        }
        if (!WIFEXITED(status)) {
            return {false, {}, "image_ocr_failed", "tesseract terminated unexpectedly"};
        }

        const int exit_code = WEXITSTATUS(status);
        if (exit_code == 127) {
            return {false, {}, "image_ocr_unavailable", "tesseract is not installed or not in PATH"};
        }
        if (exit_code != 0) {
            return {false, {}, "image_ocr_failed", "tesseract failed with exit code " + std::to_string(exit_code)};
        }
        return {true, output, {}, {}};
#else
        (void)path;
        return {false, {}, "image_ocr_unavailable", "Image OCR requires tesseract on this platform"};
#endif
    }
};

std::string stripXmlTagsToText(const std::string& xml) {
    std::string text = std::regex_replace(xml, std::regex("(?is)<w:(p|br|cr|tab)[^>]*/?>"), "\n");
    text = std::regex_replace(text, std::regex("(?is)</w:p>"), "\n");
    text = std::regex_replace(text, std::regex("(?is)<[^>]+>"), " ");
    return collapseWhitespace(htmlEntityDecode(text));
}

struct ZipEntryResult {
    bool ok = false;
    std::string data;
    std::string code;
    std::string message;
};

ZipEntryResult readZipEntry(const std::string& archive_path, const std::string& entry_path, const std::string& code_prefix) {
#if QORNIX_HAS_LIBZIP
    int zip_error = 0;
    zip_t* archive = zip_open(archive_path.c_str(), ZIP_RDONLY, &zip_error);
    if (!archive) {
        return {false, {}, code_prefix + "_open_failed", "Could not open ZIP archive"};
    }

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat(archive, entry_path.c_str(), 0, &stat) != 0) {
        zip_close(archive);
        return {false, {}, code_prefix + "_entry_missing", "ZIP archive does not contain " + entry_path};
    }

    zip_file_t* file = zip_fopen(archive, entry_path.c_str(), 0);
    if (!file) {
        zip_close(archive);
        return {false, {}, code_prefix + "_entry_open_failed", "Could not open " + entry_path};
    }

    std::string data;
    data.resize(static_cast<size_t>(stat.size));
    zip_int64_t total_read = 0;
    while (total_read < static_cast<zip_int64_t>(data.size())) {
        const auto bytes = zip_fread(file,
                                     data.data() + total_read,
                                     static_cast<zip_uint64_t>(data.size() - total_read));
        if (bytes < 0) {
            zip_fclose(file);
            zip_close(archive);
            return {false, {}, code_prefix + "_entry_read_failed", "Could not read " + entry_path};
        }
        if (bytes == 0) {
            break;
        }
        total_read += bytes;
    }

    zip_fclose(file);
    zip_close(archive);
    data.resize(static_cast<size_t>(total_read));
    return {true, data, {}, {}};
#else
    (void)archive_path;
    (void)entry_path;
    return {false, {}, code_prefix + "_parser_unavailable", "ZIP archive parsing requires libzip development libraries"};
#endif
}

#if QORNIX_HAS_XLSX || QORNIX_HAS_OPENXML
void appendNodeText(pugi::xml_node node, std::string& text) {
    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            text += child.value();
        } else {
            appendNodeText(child, text);
        }
    }
}

std::vector<std::string> parseSharedStrings(const std::string& xml) {
    std::vector<std::string> shared_strings;
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str())) {
        return shared_strings;
    }
    for (pugi::xml_node si : doc.child("sst").children("si")) {
        std::string value;
        appendNodeText(si, value);
        shared_strings.push_back(collapseWhitespace(value));
    }
    return shared_strings;
}

std::string resolveWorkbookTarget(std::string target) {
    if (target.empty()) {
        return target;
    }
    if (target.front() == '/') {
        target.erase(target.begin());
        return target;
    }
    if (target.rfind("xl/", 0) == 0) {
        return target;
    }
    return "xl/" + target;
}

std::map<std::string, std::string> parseSheetNames(const std::string& workbook_xml, const std::string& rels_xml) {
    std::map<std::string, std::string> relationship_targets;
    pugi::xml_document rels;
    if (rels.load_string(rels_xml.c_str())) {
        for (pugi::xml_node rel : rels.child("Relationships").children("Relationship")) {
            relationship_targets[rel.attribute("Id").as_string()] = resolveWorkbookTarget(rel.attribute("Target").as_string());
        }
    }

    std::map<std::string, std::string> sheet_names;
    pugi::xml_document workbook;
    if (!workbook.load_string(workbook_xml.c_str())) {
        return sheet_names;
    }
    for (pugi::xml_node sheet : workbook.child("workbook").child("sheets").children("sheet")) {
        const std::string rel_id = sheet.attribute("r:id").as_string();
        const std::string name = sheet.attribute("name").as_string();
        auto target_it = relationship_targets.find(rel_id);
        if (target_it != relationship_targets.end() && !name.empty()) {
            sheet_names[target_it->second] = name;
        }
    }
    return sheet_names;
}

std::string cellValue(pugi::xml_node cell, const std::vector<std::string>& shared_strings) {
    const std::string type = cell.attribute("t").as_string();
    if (type == "inlineStr") {
        std::string value;
        appendNodeText(cell.child("is"), value);
        return collapseWhitespace(value);
    }

    const std::string raw = cell.child("v").text().as_string();
    if (type == "s") {
        try {
            const auto index = static_cast<size_t>(std::stoul(raw));
            if (index < shared_strings.size()) {
                return shared_strings[index];
            }
        } catch (...) {
            return {};
        }
    }
    return collapseWhitespace(raw);
}

std::string resolvePresentationTarget(std::string target) {
    if (target.empty()) {
        return target;
    }
    if (target.front() == '/') {
        target.erase(target.begin());
        return target;
    }
    if (target.rfind("ppt/", 0) == 0) {
        return target;
    }
    return "ppt/" + target;
}

std::vector<std::string> parseSlidePaths(const std::string& presentation_xml, const std::string& rels_xml) {
    std::map<std::string, std::string> relationship_targets;
    pugi::xml_document rels;
    if (rels.load_string(rels_xml.c_str())) {
        for (pugi::xml_node rel : rels.child("Relationships").children("Relationship")) {
            relationship_targets[rel.attribute("Id").as_string()] = resolvePresentationTarget(rel.attribute("Target").as_string());
        }
    }

    std::vector<std::string> slide_paths;
    pugi::xml_document presentation;
    if (!presentation.load_string(presentation_xml.c_str())) {
        return slide_paths;
    }
    for (pugi::xml_node slide : presentation.child("p:presentation").child("p:sldIdLst").children("p:sldId")) {
        const std::string rel_id = slide.attribute("r:id").as_string();
        auto target_it = relationship_targets.find(rel_id);
        if (target_it != relationship_targets.end()) {
            slide_paths.push_back(target_it->second);
        }
    }
    return slide_paths;
}

void collectPresentationText(pugi::xml_node node, std::vector<std::string>& values) {
    for (pugi::xml_node child : node.children()) {
        const std::string name = child.name();
        if (name == "a:t" || name == "t") {
            values.push_back(collapseWhitespace(child.text().as_string()));
        } else {
            collectPresentationText(child, values);
        }
    }
}
#endif

class DocxParser final : public DocumentParser {
public:
    std::string id() const override { return "docx_libzip"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.extension == ".docx";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

        const auto extracted = extractDocumentXml(path);
        if (!extracted.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, extracted.code, extracted.message});
            return std::nullopt;
        }

        const auto text = stripXmlTagsToText(extracted.xml);
        if (text.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_docx_text", "DOCX produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = text;
        doc.document_type = "docx";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["docx_archive_backend"] = "libzip";
        return doc;
    }

private:
    struct ExtractResult {
        bool ok = false;
        std::string xml;
        std::string code;
        std::string message;
    };

    static ExtractResult extractDocumentXml(const std::string& path) {
#if QORNIX_HAS_LIBZIP
        int zip_error = 0;
        zip_t* archive = zip_open(path.c_str(), ZIP_RDONLY, &zip_error);
        if (!archive) {
            return {false, {}, "docx_open_failed", "Could not open DOCX archive"};
        }

        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat(archive, "word/document.xml", 0, &stat) != 0) {
            zip_close(archive);
            return {false, {}, "docx_document_xml_missing", "DOCX archive does not contain word/document.xml"};
        }

        zip_file_t* file = zip_fopen(archive, "word/document.xml", 0);
        if (!file) {
            zip_close(archive);
            return {false, {}, "docx_document_xml_open_failed", "Could not open word/document.xml"};
        }

        std::string xml;
        xml.resize(static_cast<size_t>(stat.size));
        zip_int64_t total_read = 0;
        while (total_read < static_cast<zip_int64_t>(xml.size())) {
            const auto bytes = zip_fread(file,
                                         xml.data() + total_read,
                                         static_cast<zip_uint64_t>(xml.size() - total_read));
            if (bytes < 0) {
                zip_fclose(file);
                zip_close(archive);
                return {false, {}, "docx_document_xml_read_failed", "Could not read word/document.xml"};
            }
            if (bytes == 0) {
                break;
            }
            total_read += bytes;
        }
        zip_fclose(file);
        zip_close(archive);
        xml.resize(static_cast<size_t>(total_read));
        return {true, xml, {}, {}};
#else
        (void)path;
        return {false, {}, "docx_parser_unavailable", "DOCX ingestion requires libzip development libraries"};
#endif
    }
};

class XlsxParser final : public DocumentParser {
public:
    std::string id() const override { return "xlsx_libzip_pugixml"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.extension == ".xlsx";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

#if QORNIX_HAS_XLSX
        const auto workbook = readZipEntry(path, "xl/workbook.xml", "xlsx_workbook");
        if (!workbook.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, workbook.code, workbook.message});
            return std::nullopt;
        }

        const auto rels = readZipEntry(path, "xl/_rels/workbook.xml.rels", "xlsx_workbook_rels");
        const auto shared = readZipEntry(path, "xl/sharedStrings.xml", "xlsx_shared_strings");
        const auto shared_strings = shared.ok ? parseSharedStrings(shared.data) : std::vector<std::string>{};
        const auto sheet_names = rels.ok ? parseSheetNames(workbook.data, rels.data) : std::map<std::string, std::string>{};

        std::stringstream text;
        size_t sheet_count = 0;
        size_t row_count = 0;
        size_t cell_count = 0;
        std::vector<std::string> names;

        for (const auto& [entry_path, sheet_name] : sheet_names) {
            const auto sheet = readZipEntry(path, entry_path, "xlsx_sheet");
            if (!sheet.ok) {
                continue;
            }

            pugi::xml_document sheet_doc;
            if (!sheet_doc.load_string(sheet.data.c_str())) {
                continue;
            }

            ++sheet_count;
            names.push_back(sheet_name);
            text << "Sheet: " << sheet_name << "\n";
            for (pugi::xml_node row : sheet_doc.child("worksheet").child("sheetData").children("row")) {
                std::vector<std::string> fields;
                for (pugi::xml_node cell : row.children("c")) {
                    fields.push_back(cellValue(cell, shared_strings));
                    ++cell_count;
                }
                if (!fields.empty()) {
                    ++row_count;
                    text << "Row " << row.attribute("r").as_string() << ": " << joinFields(fields, " | ") << "\n";
                }
            }
            text << "\n";
        }

        const auto content = normalizeExtractedTextPreservingLines(text.str());
        if (content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_xlsx_text", "XLSX produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = content;
        doc.document_type = "xlsx";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["xlsx_archive_backend"] = "libzip";
        doc.metadata["xlsx_xml_parser"] = "pugixml";
        doc.metadata["xlsx_sheet_count"] = std::to_string(sheet_count);
        doc.metadata["xlsx_row_count"] = std::to_string(row_count);
        doc.metadata["xlsx_cell_count"] = std::to_string(cell_count);
        doc.metadata["xlsx_sheet_names"] = joinFields(names, ",");
        doc.metadata["structure_contract"] = "spreadsheet_rows_v1";
        doc.metadata["table_sheet_count"] = std::to_string(sheet_count);
        doc.metadata["table_row_count"] = std::to_string(row_count);
        doc.metadata["table_column_count"] = std::to_string(cell_count);
        return doc;
#else
        result.skipped++;
        result.issues.push_back({
            IngestionIssueSeverity::WARNING,
            path,
            "xlsx_parser_unavailable",
            "XLSX ingestion requires libzip and pugixml development libraries"
        });
        return std::nullopt;
#endif
    }
};

class PptxParser final : public DocumentParser {
public:
    std::string id() const override { return "pptx_libzip_pugixml"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.extension == ".pptx";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

#if QORNIX_HAS_OPENXML
        const auto presentation = readZipEntry(path, "ppt/presentation.xml", "pptx_presentation");
        if (!presentation.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, presentation.code, presentation.message});
            return std::nullopt;
        }

        const auto rels = readZipEntry(path, "ppt/_rels/presentation.xml.rels", "pptx_presentation_rels");
        if (!rels.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, rels.code, rels.message});
            return std::nullopt;
        }

        const auto slide_paths = parseSlidePaths(presentation.data, rels.data);
        std::stringstream text;
        size_t slide_count = 0;
        size_t text_run_count = 0;

        for (const auto& slide_path : slide_paths) {
            const auto slide = readZipEntry(path, slide_path, "pptx_slide");
            if (!slide.ok) {
                continue;
            }

            pugi::xml_document slide_doc;
            if (!slide_doc.load_string(slide.data.c_str())) {
                continue;
            }

            std::vector<std::string> values;
            collectPresentationText(slide_doc, values);
            std::vector<std::string> non_empty_values;
            for (const auto& value : values) {
                if (!value.empty()) {
                    non_empty_values.push_back(value);
                }
            }
            if (non_empty_values.empty()) {
                continue;
            }

            ++slide_count;
            text_run_count += non_empty_values.size();
            text << "Slide " << slide_count << ": " << joinFields(non_empty_values, " | ") << "\n";
        }

        const auto content = collapseWhitespace(text.str());
        if (content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_pptx_text", "PPTX produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = content;
        doc.document_type = "pptx";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["pptx_archive_backend"] = "libzip";
        doc.metadata["pptx_xml_parser"] = "pugixml";
        doc.metadata["pptx_slide_count"] = std::to_string(slide_count);
        doc.metadata["pptx_text_run_count"] = std::to_string(text_run_count);
        return doc;
#else
        result.skipped++;
        result.issues.push_back({
            IngestionIssueSeverity::WARNING,
            path,
            "pptx_parser_unavailable",
            "PPTX ingestion requires libzip and pugixml development libraries"
        });
        return std::nullopt;
#endif
    }
};

} // namespace

IngestionPipeline::IngestionPipeline(Config config)
    : config_(std::move(config)) {
    if (config_.include_extensions.empty()) {
        config_.include_extensions = defaultExtensions();
    }
    for (auto& extension : config_.include_extensions) {
        extension = toLower(extension);
    }
    if (config_.exclude_directories.empty()) {
        config_.exclude_directories = {
            "cmake-build", ".git", "build", "__pycache__",
            "node_modules", ".venv", "dist", "bin", "obj",
            "qornix_rag/models", "/models/"
        };
    }
    parsers_.push_back(std::make_shared<DocxParser>());
    parsers_.push_back(std::make_shared<XlsxParser>());
    parsers_.push_back(std::make_shared<PptxParser>());
    parsers_.push_back(std::make_shared<PdfParser>());
    parsers_.push_back(std::make_shared<ImageOcrParser>());
    parsers_.push_back(std::make_shared<HtmlParser>());
    parsers_.push_back(std::make_shared<CsvParser>());
    parsers_.push_back(std::make_shared<PlainTextParser>());
}

DetectedDocumentType IngestionPipeline::detectFileType(const std::string& path) const {
    DetectedDocumentType detected;
    detected.extension = toLower(fs::path(path).extension().string());

    const auto& mapping = textExtensionMap();
    auto it = mapping.find(detected.extension);
    if (it == mapping.end()) {
        detected.supported = false;
        detected.reason = "unsupported_extension";
        return detected;
    }

    detected.mime_type = it->second.first;
    detected.language = it->second.second;
    if (detected.mime_type == "text/html") {
        detected.document_type = "html";
    } else if (detected.mime_type == "application/pdf") {
        detected.document_type = "pdf";
        detected.binary = true;
    } else if (detected.extension == ".docx") {
        detected.document_type = "docx";
        detected.binary = true;
    } else if (detected.extension == ".xlsx") {
        detected.document_type = "xlsx";
        detected.binary = true;
    } else if (detected.extension == ".pptx") {
        detected.document_type = "pptx";
        detected.binary = true;
    } else if (detected.mime_type.rfind("image/", 0) == 0) {
        detected.document_type = "image";
        detected.binary = true;
    } else if (detected.extension == ".csv") {
        detected.document_type = "csv";
    } else {
        detected.document_type = sourceExtensions().count(detected.extension) > 0 ? "source" : "config";
    }
    detected.supported = isAllowedExtension(detected.extension);
    detected.reason = detected.supported ? "supported_text" : "extension_not_enabled";
    return detected;
}

bool IngestionPipeline::shouldSkipDirectory(const std::string& path) const {
    for (const auto& skip : config_.exclude_directories) {
        if (!skip.empty() && path.find(skip) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string IngestionPipeline::determineRelativePath(const std::string& full_path) const {
    try {
        return fs::relative(full_path, config_.root_path).string();
    } catch (...) {
        return full_path;
    }
}

bool IngestionPipeline::isAllowedExtension(const std::string& extension) const {
    return std::find(config_.include_extensions.begin(),
                     config_.include_extensions.end(),
                     toLower(extension)) != config_.include_extensions.end();
}

const DocumentParser* IngestionPipeline::findParser(const DetectedDocumentType& detected) const {
    for (const auto& parser : parsers_) {
        if (parser && parser->supports(detected)) {
            return parser.get();
        }
    }
    return nullptr;
}

std::optional<IngestedDocument> IngestionPipeline::parseFile(
    const std::string& path,
    const DetectedDocumentType& detected,
    IngestionJobResult& result) const {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            result.errors++;
            result.issues.push_back({IngestionIssueSeverity::ERROR, path, "open_failed", "Could not open file"});
            return std::nullopt;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        const auto* parser = findParser(detected);
        if (!parser) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "parser_not_found", "No parser registered for file type"});
            return std::nullopt;
        }

        auto parsed = parser->parse(path, detected, content, result);
        if (parsed && parsed->relative_path.empty()) {
            parsed->relative_path = determineRelativePath(path);
        }
        return parsed;
    } catch (const std::exception& e) {
        result.errors++;
        result.issues.push_back({IngestionIssueSeverity::ERROR, path, "parse_failed", e.what()});
        return std::nullopt;
    }
}

IngestionJobResult IngestionPipeline::ingestPath(const std::string& path) const {
    IngestionJobResult result;
    result.files_seen = 1;

    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        result.skipped++;
        result.issues.push_back({IngestionIssueSeverity::WARNING, path, "not_regular_file", "Path is not a regular file"});
        return result;
    }

    const auto detected = detectFileType(path);
    if (!detected.supported) {
        result.skipped++;
        result.issues.push_back({IngestionIssueSeverity::INFO, path, detected.reason, "Unsupported file type"});
        return result;
    }

    try {
        const auto max_bytes = config_.max_file_size_kb * 1024;
        if (fs::file_size(path) > max_bytes) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "file_too_large", "File exceeds max size"});
            return result;
        }
    } catch (...) {
        result.skipped++;
        result.issues.push_back({IngestionIssueSeverity::WARNING, path, "file_size_failed", "Could not read file size"});
        return result;
    }

    auto parsed = parseFile(path, detected, result);
    if (parsed) {
        result.documents.push_back(std::move(*parsed));
        result.documents_imported++;
    }
    return result;
}

IngestionJobResult IngestionPipeline::ingestRoot() const {
    IngestionJobResult result;
    std::unordered_set<std::string> seen_hashes;

    std::error_code ec;
    if (!fs::exists(config_.root_path, ec)) {
        result.errors++;
        result.issues.push_back({IngestionIssueSeverity::ERROR, config_.root_path, "root_missing", "Root path does not exist"});
        return result;
    }

    auto ingest_file = [&](const fs::directory_entry& entry) {
        if (!entry.is_regular_file()) {
            return;
        }
        const auto path = entry.path().string();
        auto single = ingestPath(path);
        result.files_seen += single.files_seen;
        result.skipped += single.skipped;
        result.errors += single.errors;
        result.issues.insert(result.issues.end(), single.issues.begin(), single.issues.end());

        for (auto& doc : single.documents) {
            if (!seen_hashes.insert(doc.hash).second) {
                result.duplicates_found++;
                continue;
            }
            result.documents.push_back(std::move(doc));
            result.documents_imported++;
        }
    };

    try {
        if (config_.recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(config_.root_path)) {
                if (shouldSkipDirectory(entry.path().parent_path().string())) {
                    continue;
                }
                ingest_file(entry);
            }
        } else {
            for (const auto& entry : fs::directory_iterator(config_.root_path)) {
                ingest_file(entry);
            }
        }
    } catch (const std::exception& e) {
        result.errors++;
        result.issues.push_back({IngestionIssueSeverity::ERROR, config_.root_path, "scan_failed", e.what()});
    }

    return result;
}

std::string ingestionIssueSeverityToString(IngestionIssueSeverity severity) {
    switch (severity) {
        case IngestionIssueSeverity::INFO: return "info";
        case IngestionIssueSeverity::WARNING: return "warning";
        case IngestionIssueSeverity::ERROR: return "error";
    }
    return "unknown";
}

} // namespace qornix::rag
