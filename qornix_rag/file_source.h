/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Qornix RAG - Filesystem Data Source
 *
 * Scans directories and indexes files by extension.
 * Refactored from RagEngine's index_project() method.
 */

#include "data_source.h"
#include "ingestion_pipeline.h"
#include <set>

namespace qornix {
namespace rag {

/**
 * Filesystem-based data source.
 * Scans directories and indexes files by extension.
 *
 * Usage:
 *   FileSource::Config config;
 *   config.root_path = "/path/to/project";
 *   config.include_extensions = {".cpp", ".h", ".py", ".md"};
 *   config.exclude_directories = {".git", "build"};
 *   auto source = std::make_shared<FileSource>(config);
 *   engine.addDataSource(source);
 */
class FileSource : public DataSource {
public:
    struct Config {
        std::string root_path = ".";
        std::vector<std::string> include_extensions;
        std::vector<std::string> exclude_directories;
        size_t max_file_size_kb = 512;
        bool recursive = true;
        std::string source_id = "fs_default";
        std::string name = "Filesystem Source";
    };

    explicit FileSource(Config config);
    ~FileSource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::FILESYSTEM; }
    size_t count() const override;
    bool initialize() override;
    void cleanup() override;
    std::string getId() const override { return config_.source_id; }
    std::string getName() const override { return config_.name; }

    // Update root path at runtime
    void setRootPath(const std::string& path);

    // Get excluded directories list
    const std::vector<std::string>& getExcludeDirectories() const { return config_.exclude_directories; }

    // Add/Remove excluded directory
    void addExcludeDirectory(const std::string& dir);
    void removeExcludeDirectory(const std::string& dir);

private:
    bool shouldSkipDirectory(const std::string& path) const;
    bool isAllowedExtension(const std::string& ext) const;
    std::string getLanguageFromExtension(const std::string& ext) const;
    IngestionPipeline::Config makeIngestionConfig() const;
    Document fromIngestedDocument(const IngestedDocument& ingested) const;
    Document readFile(const std::string& path, const std::string& type,
                      const std::string& language) const;
    std::string determineRelativePath(const std::string& full_path) const;

    Config config_;
    std::vector<Document> cached_docs_;
    bool initialized_ = false;

    // Language detection map
    static const std::map<std::string, std::string>& getLangMap();
};

} // namespace rag
} // namespace qornix
