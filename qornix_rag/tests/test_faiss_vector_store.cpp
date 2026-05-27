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
    VectorStoreOptions options;
    options.backend = "faiss";
    auto store = createVectorStore(options);
    assert(store);
    assert(store->backendName() == "faiss");

    const std::vector<VectorRecord> records = {
        {11, {1.0f, 0.0f, 0.0f}},
        {22, {0.0f, 1.0f, 0.0f}},
        {33, {0.0f, 0.0f, 1.0f}},
    };
    assert(store->build(records, 3));
    assert(store->isReady());
    assert(store->diagnostics().status == "ready");

    auto hits = store->search({0.0f, 1.0f, 0.0f}, 2);
    assert(!hits.empty());
    assert(hits.front().label == 22);

    const auto path = fs::temp_directory_path()
        / ("qornix_faiss_vector_store_" + std::to_string(::getpid()) + ".index");
    fs::remove(path);
    fs::remove(path.string() + ".labels");
    assert(store->save(path.string()));
    assert(fs::exists(path));
    assert(fs::exists(path.string() + ".labels"));

    auto loaded = createVectorStore(options);
    assert(loaded);
    assert(loaded->load(path.string(), 3));
    assert(loaded->isReady());
    auto loaded_hits = loaded->search({1.0f, 0.0f, 0.0f}, 2);
    assert(!loaded_hits.empty());
    assert(loaded_hits.front().label == 11);

    fs::remove(path);
    fs::remove(path.string() + ".labels");
    std::cout << "Faiss vector store tests passed\n";
    return 0;
}
