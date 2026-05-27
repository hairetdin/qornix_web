/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "vector_store.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

std::vector<VectorRecord> records() {
    return {
        {101, {1.0f, 0.0f, 0.0f}},
        {202, {0.0f, 1.0f, 0.0f}},
        {303, {0.0f, 0.0f, 1.0f}},
    };
}

void runQdrantIfConfigured() {
    const char* endpoint = std::getenv("QORNIX_TEST_QDRANT_URL");
    if (!endpoint || std::string(endpoint).empty()) {
        std::cout << "Skipping Qdrant live test: QORNIX_TEST_QDRANT_URL is not set\n";
        return;
    }

    VectorStoreOptions options;
    options.backend = "qdrant";
    options.endpoint = endpoint;
    options.collection = "qornix_live_vectors_" + std::to_string(::getpid());
    options.distance = "Cosine";
    options.recreate = true;
    options.upsert_batch_size = 2;

    auto store = createVectorStore(options);
    assert(store);
    assert(store->build(records(), 3));
    assert(store->isReady());
    auto diagnostics = store->diagnostics();
    assert(diagnostics.status == "ready");
    assert(diagnostics.size == 3);

    auto hits = store->search({1.0f, 0.0f, 0.0f}, 2);
    assert(!hits.empty());
    assert(hits.front().label == 101);
}

void runPgVectorIfConfigured() {
    const char* dsn = std::getenv("QORNIX_TEST_PGVECTOR_DSN");
    if (!dsn || std::string(dsn).empty()) {
        std::cout << "Skipping pgvector live test: QORNIX_TEST_PGVECTOR_DSN is not set\n";
        return;
    }

    VectorStoreOptions options;
    options.backend = "pgvector";
    options.connection_string = dsn;
    options.table = "qornix_live_vectors_" + std::to_string(::getpid());
    options.recreate = true;
    options.upsert_batch_size = 2;

    auto store = createVectorStore(options);
    assert(store);
    assert(store->build(records(), 3));
    assert(store->isReady());
    auto diagnostics = store->diagnostics();
    assert(diagnostics.status == "ready");
    assert(diagnostics.size == 3);

    auto hits = store->search({0.0f, 1.0f, 0.0f}, 2);
    assert(!hits.empty());
    assert(hits.front().label == 202);
}

} // namespace

int main() {
    runQdrantIfConfigured();
    runPgVectorIfConfigured();
    std::cout << "Live vector store tests completed\n";
    return 0;
}
