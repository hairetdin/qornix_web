/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Qornix RAG - Text Documents Data Source
 *
 * Load and index arbitrary text files (markdown, txt, rst, adoc, etc.).
 * Useful for documentation, articles, manuals, and other text-based knowledge.
 */

#include "data_source.h"

namespace qornix {
namespace rag {

/**
 * Text documents data source.
 * Load and index arbitrary text files (markdown, txt, rst, adoc, etc.).
 *
 * Usage:
 *   TextSource::Config config;
 *   config.directory_path = "/path/to/docs";
 *   config.extensions = {".md", ".txt", ".rst"};
 *   auto source = std::make_shared<TextSource>(config);
 *   engine.addDataSource(source);
 */
class TextSource : public DataSource {
public:
    struct Config {
        std::vector<std::string> file_paths;      // Explicit file list
        std::string directory_path;                // Or directory to scan
        std::vector<std::string> extensions;       // File extensions to include
        bool recursive = true;                     // Scan subdirectories
        size_t max_file_size_kb = 1024;            // Larger limit for docs
        std::string source_id = "text_default";
        std::string name = "Text Documents Source";
    };

    explicit TextSource(Config config);
    ~TextSource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::TEXT_DOCS; }
    size_t count() const override;
    bool initialize() override;
    void cleanup() override;
    std::string getId() const override { return config_.source_id; }
    std::string getName() const override { return config_.name; }

    // API to add files dynamically
    void addFile(const std::string& path);
    void removeFile(const std::string& path);
    void addFiles(const std::vector<std::string>& paths);

    // Update configuration
    void setDirectoryPath(const std::string& path);
    void setExtensions(const std::vector<std::string>& extensions);

private:
    bool isAllowedExtension(const std::string& ext) const;
    Document readFile(const std::string& path) const;

    Config config_;
    std::vector<Document> cached_docs_;
    bool initialized_ = false;
};

} // namespace rag
} // namespace qornix
