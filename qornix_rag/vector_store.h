/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct VectorRecord {
    size_t label = 0;
    std::vector<float> embedding;
};

struct VectorSearchHit {
    size_t label = 0;
    float distance = 0.0f;
    double score = 0.0;
};

class VectorStore {
public:
    virtual ~VectorStore() = default;

    virtual std::string backendName() const = 0;
    virtual bool build(const std::vector<VectorRecord>& records, size_t dimension) = 0;
    virtual std::vector<VectorSearchHit> search(const std::vector<float>& query, size_t top_k) const = 0;
    virtual bool save(const std::string& path) const = 0;
    virtual bool load(const std::string& path, size_t dimension, size_t max_elements = 0) = 0;
    virtual void clear() = 0;
    virtual bool isReady() const = 0;
    virtual size_t size() const = 0;
    virtual size_t dimension() const = 0;
};

class LocalHnswVectorStore final : public VectorStore {
public:
    LocalHnswVectorStore();
    ~LocalHnswVectorStore() override;

    std::string backendName() const override;
    bool build(const std::vector<VectorRecord>& records, size_t dimension) override;
    std::vector<VectorSearchHit> search(const std::vector<float>& query, size_t top_k) const override;
    bool save(const std::string& path) const override;
    bool load(const std::string& path, size_t dimension, size_t max_elements = 0) override;
    void clear() override;
    bool isReady() const override;
    size_t size() const override;
    size_t dimension() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
