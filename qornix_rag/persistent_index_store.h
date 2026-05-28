/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "core.h"

#include <optional>
#include <string>
#include <vector>

struct PersistedIndexStats {
    size_t documents = 0;
    size_t chunks = 0;
    size_t embeddings = 0;
    size_t deleted_documents = 0;
};

struct PersistedEmbeddingRecord {
    std::string chunk_id;
    std::string source_id;
    std::string model_id;
    std::string backend;
    std::string content_hash;
    VectorRecord vector;
};

class PersistentIndexStore {
public:
    virtual ~PersistentIndexStore() = default;

    virtual PersistedIndexStats persistIndexedDocuments(const std::vector<Document>& documents,
                                                        const std::string& source_id = "",
                                                        const std::string& embedding_model_id = "",
                                                        const std::string& embedding_backend = "") = 0;
    virtual size_t countPersistedDocuments(const std::string& source_id = "") const = 0;
    virtual size_t countPersistedChunks(const std::string& source_id = "") const = 0;
    virtual size_t countPersistedEmbeddings(const std::string& source_id = "") const = 0;
    virtual std::optional<Document> findPersistedDocument(const std::string& relative_path,
                                                          const std::string& source_id = "") const = 0;
    virtual std::vector<PersistedEmbeddingRecord> listPersistedEmbeddings(
        const std::string& source_id = "",
        const std::string& model_id = "",
        size_t limit = 0,
        size_t offset = 0) const = 0;
};
