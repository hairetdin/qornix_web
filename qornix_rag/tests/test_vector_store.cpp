/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "vector_store.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

int main() {
    const std::vector<VectorRecord> records = {
        {10, {1.0f, 0.0f, 0.0f}},
        {20, {0.0f, 1.0f, 0.0f}},
        {30, {0.0f, 0.0f, 1.0f}},
    };

    LocalHnswVectorStore store;
    assert(store.backendName() == "local_hnsw");
    assert(store.build(records, 3));
    assert(store.isReady());
    assert(store.size() == 3);
    assert(store.dimension() == 3);

    auto hits = store.search({1.0f, 0.0f, 0.0f}, 2);
    assert(!hits.empty());
    assert(hits.front().label == 10);
    assert(hits.front().score > 0.0);

    auto path = fs::temp_directory_path() / ("qornix_vector_store_" + std::to_string(::getpid()) + ".bin");
    fs::remove(path);
    assert(store.save(path.string()));
    assert(fs::exists(path));

    LocalHnswVectorStore loaded;
    assert(loaded.load(path.string(), 3));
    assert(loaded.isReady());
    assert(loaded.size() == 3);
    auto loaded_hits = loaded.search({0.0f, 1.0f, 0.0f}, 2);
    assert(!loaded_hits.empty());
    assert(loaded_hits.front().label == 20);

    fs::remove(path);
    std::cout << "Vector store tests passed\n";
    return 0;
}
