/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Qornix RAG - In-Memory Data Source
 *
 * Documents are added programmatically, useful for:
 * - Testing
 * - Dynamic content (web forms, API uploads)
 * - Temporary knowledge bases
 */

#include "data_source.h"

namespace qornix {
namespace rag {

/**
 * In-memory data source.
 * Documents are added programmatically, useful for:
 * - Testing
 * - Dynamic content (web forms, API uploads)
 * - Temporary knowledge bases
 *
 * Usage:
 *   MemorySource::Config config;
 *   config.name = "test_memory";
 *   MemorySource source(config);
 *
 *   Document doc;
 *   doc.path = "test://1";
 *   doc.relative_path = "test://1";
 *   doc.content = "Test content";
 *   source.addDocument(doc);
 *
 *   engine.addDataSource(std::make_shared<MemorySource>(config));
 */
class MemorySource : public DataSource {
public:
    struct Config {
        std::string name = "Memory Source";
        std::string source_id = "mem_default";
        bool allow_duplicates = false;
    };

    explicit MemorySource(Config config);
    ~MemorySource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::MEMORY; }
    size_t count() const override;
    bool initialize() override { return true; }
    void cleanup() override;
    std::string getId() const override { return config_.source_id; }
    std::string getName() const override { return config_.name; }

    // Bulk operations
    /**
     * Add multiple documents at once.
     */
    void addDocuments(const std::vector<Document>& docs);

    /**
     * Clear all documents.
     */
    void clear();

    /**
     * Check if a document with given ID exists.
     */
    bool contains(const std::string& id) const;

    /**
     * Get a document by ID (if exists).
     */
    std::optional<Document> getDocument(const std::string& id) const;

    // Update configuration
    void setAllowDuplicates(bool allow);

private:
    Config config_;
    std::vector<Document> documents_;
    mutable std::mutex mutex_;
};

} // namespace rag
} // namespace qornix
