/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qornix::rag {

enum class IngestionIssueSeverity {
    INFO,
    WARNING,
    ERROR
};

struct DetectedDocumentType {
    std::string extension;
    std::string mime_type = "application/octet-stream";
    std::string document_type = "unsupported";
    std::string language = "unknown";
    bool supported = false;
    bool binary = false;
    std::string reason;
};

struct IngestedDocument {
    std::string path;
    std::string relative_path;
    std::string content;
    std::string document_type;
    std::string language;
    std::string mime_type;
    std::string hash;
    size_t size_bytes = 0;
    size_t lines_count = 0;
    std::chrono::system_clock::time_point last_modified;
    std::map<std::string, std::string> metadata;
};

struct IngestionIssue {
    IngestionIssueSeverity severity = IngestionIssueSeverity::INFO;
    std::string path;
    std::string code;
    std::string message;
};

struct IngestionJobResult {
    size_t files_seen = 0;
    size_t documents_imported = 0;
    size_t duplicates_found = 0;
    size_t skipped = 0;
    size_t errors = 0;
    std::vector<IngestedDocument> documents;
    std::vector<IngestionIssue> issues;
};

class DocumentParser {
public:
    virtual ~DocumentParser() = default;
    virtual std::string id() const = 0;
    virtual bool supports(const DetectedDocumentType& detected) const = 0;
    virtual std::optional<IngestedDocument> parse(const std::string& path,
                                                  const DetectedDocumentType& detected,
                                                  const std::string& raw_content,
                                                  IngestionJobResult& result) const = 0;
};

class IngestionPipeline {
public:
    struct Config {
        std::string root_path = ".";
        std::vector<std::string> include_extensions;
        std::vector<std::string> exclude_directories;
        size_t max_file_size_kb = 512;
        bool recursive = true;
    };

    explicit IngestionPipeline(Config config);

    const Config& config() const { return config_; }

    DetectedDocumentType detectFileType(const std::string& path) const;
    IngestionJobResult ingestPath(const std::string& path) const;
    IngestionJobResult ingestRoot() const;

    bool shouldSkipDirectory(const std::string& path) const;
    std::string determineRelativePath(const std::string& full_path) const;

private:
    Config config_;
    std::vector<std::shared_ptr<DocumentParser>> parsers_;

    bool isAllowedExtension(const std::string& extension) const;
    const DocumentParser* findParser(const DetectedDocumentType& detected) const;
    std::optional<IngestedDocument> parseFile(const std::string& path,
                                              const DetectedDocumentType& detected,
                                              IngestionJobResult& result) const;
};

std::string ingestionIssueSeverityToString(IngestionIssueSeverity severity);

} // namespace qornix::rag
