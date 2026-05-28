/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qornix::rag {

struct RagUploadConfig {
    bool enabled = true;
    std::string uploads_dir = "qornix_rag/data/uploads";
    size_t max_file_size_kb = 16384;
    size_t max_files_per_request = 20;
    bool auto_ingest = true;
    bool async_ingest = false;
    bool overwrite_existing = false;
    std::vector<std::string> allowed_extensions = {
        ".txt", ".md", ".rst", ".adoc",
        ".json", ".yaml", ".yml", ".xml", ".html", ".htm",
        ".csv", ".pdf", ".docx", ".xlsx", ".pptx",
        ".png", ".jpg", ".jpeg", ".tif", ".tiff", ".bmp", ".webp"
    };
    std::vector<std::string> allowed_mime_types = {
        "text/plain", "text/markdown", "text/csv", "text/html",
        "application/json", "application/xml", "application/pdf", "application/x-yaml", "application/yaml",
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        "application/vnd.openxmlformats-officedocument.presentationml.presentation",
        "image/png", "image/jpeg", "image/tiff", "image/bmp", "image/webp"
    };
};

struct RagUploadPart {
    std::string field_name;
    std::string filename;
    std::string content_type;
    std::string content;
};

struct RagStoredUpload {
    std::string original_filename;
    std::string stored_filename;
    std::string path;
    std::string relative_path;
    std::string content_type;
    size_t size_bytes = 0;
    std::string content_hash;
};

struct RagUploadValidationResult {
    bool ok = false;
    std::string code;
    std::string message;
    std::string sanitized_filename;
    std::string extension;
    std::string normalized_mime;
};

struct RagMultipartParseResult {
    bool ok = false;
    std::string error;
    std::vector<RagUploadPart> files;
    std::map<std::string, std::string> fields;
};

class RagUploadService {
public:
    explicit RagUploadService(RagUploadConfig config = RagUploadConfig());

    const RagUploadConfig& config() const { return config_; }

    RagUploadValidationResult validateFile(const std::string& filename,
                                           const std::string& content_type,
                                           size_t size_bytes) const;

    RagMultipartParseResult parseMultipart(const std::string& content_type_header,
                                           const std::string& body) const;

    std::vector<RagStoredUpload> storeFiles(const std::vector<RagUploadPart>& parts,
                                            std::string* batch_id = nullptr) const;

    bool removeStoredFile(const std::string& relative_or_absolute_path,
                          std::string* removed_absolute_path = nullptr,
                          std::string* removed_relative_path = nullptr) const;

private:
    RagUploadConfig config_;
};

std::vector<std::string> splitUploadCsvList(const std::string& csv);
std::string normalizeUploadExtension(const std::string& filename);
std::string sanitizeUploadFilename(const std::string& filename);
std::string normalizeUploadMime(const std::string& content_type);

} // namespace qornix::rag
