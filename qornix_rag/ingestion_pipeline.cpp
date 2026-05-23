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
    parsers_.push_back(std::make_shared<HtmlParser>());
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
