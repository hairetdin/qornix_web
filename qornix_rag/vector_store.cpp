/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "vector_store.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <queue>
#include <stdexcept>

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunknown-pragmas"
    #pragma clang diagnostic ignored "-Wbuiltin-macro-redefined"
    #pragma clang diagnostic ignored "-Wmacro-redefined"
#endif

#include <hnswlib/hnswlib.h>

#ifdef __clang__
    #pragma clang diagnostic pop
#endif

struct LocalHnswVectorStore::Impl {
    std::unique_ptr<hnswlib::L2Space> space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index;
    size_t dimension = 0;
    size_t count = 0;
};

LocalHnswVectorStore::LocalHnswVectorStore()
    : impl_(std::make_unique<Impl>()) {
}

LocalHnswVectorStore::~LocalHnswVectorStore() = default;

std::string LocalHnswVectorStore::backendName() const {
    return "local_hnsw";
}

bool LocalHnswVectorStore::build(const std::vector<VectorRecord>& records, size_t dimension) {
    clear();
    if (dimension == 0 || records.empty()) {
        return false;
    }

    try {
        impl_->space = std::make_unique<hnswlib::L2Space>(dimension);
        impl_->index = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            impl_->space.get(),
            records.size(),
            16,
            200
        );

        size_t added = 0;
        for (const auto& record : records) {
            if (record.embedding.size() != dimension) {
                continue;
            }
            impl_->index->addPoint(record.embedding.data(), static_cast<hnswlib::labeltype>(record.label));
            ++added;
        }

        if (added == 0) {
            clear();
            return false;
        }

        impl_->dimension = dimension;
        impl_->count = added;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error building local HNSW vector store: " << e.what() << std::endl;
        clear();
        return false;
    }
}

std::vector<VectorSearchHit> LocalHnswVectorStore::search(const std::vector<float>& query, size_t top_k) const {
    std::vector<VectorSearchHit> hits;
    if (!isReady() || query.size() != impl_->dimension || top_k == 0) {
        return hits;
    }

    try {
        const size_t k = std::min(top_k, impl_->count);
        auto result = impl_->index->searchKnn(query.data(), k);
        hits.reserve(result.size());

        while (!result.empty()) {
            auto top = result.top();
            const auto label = static_cast<size_t>(top.second);
            const float distance = top.first;
            hits.push_back(VectorSearchHit{
                label,
                distance,
                1.0 / (1.0 + static_cast<double>(distance))
            });
            result.pop();
        }

        std::reverse(hits.begin(), hits.end());
    } catch (const std::exception& e) {
        std::cerr << "Local HNSW vector search error: " << e.what() << std::endl;
        hits.clear();
    }

    return hits;
}

bool LocalHnswVectorStore::save(const std::string& path) const {
    if (!isReady() || path.empty()) {
        return false;
    }

    try {
        const std::filesystem::path index_path(path);
        if (index_path.has_parent_path()) {
            std::filesystem::create_directories(index_path.parent_path());
        }
        impl_->index->saveIndex(path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving local HNSW vector store: " << e.what() << std::endl;
        return false;
    }
}

bool LocalHnswVectorStore::load(const std::string& path, size_t dimension, size_t max_elements) {
    clear();
    if (path.empty() || dimension == 0 || !std::filesystem::exists(path)) {
        return false;
    }

    try {
        impl_->space = std::make_unique<hnswlib::L2Space>(dimension);
        impl_->index = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            impl_->space.get(),
            path,
            false,
            max_elements
        );
        impl_->dimension = dimension;
        impl_->count = impl_->index->cur_element_count;
        return impl_->count > 0;
    } catch (const std::exception& e) {
        std::cerr << "Error loading local HNSW vector store: " << e.what() << std::endl;
        clear();
        return false;
    }
}

void LocalHnswVectorStore::clear() {
    impl_->index.reset();
    impl_->space.reset();
    impl_->dimension = 0;
    impl_->count = 0;
}

bool LocalHnswVectorStore::isReady() const {
    return impl_->index != nullptr && impl_->space != nullptr && impl_->dimension > 0 && impl_->count > 0;
}

size_t LocalHnswVectorStore::size() const {
    return impl_->count;
}

size_t LocalHnswVectorStore::dimension() const {
    return impl_->dimension;
}
