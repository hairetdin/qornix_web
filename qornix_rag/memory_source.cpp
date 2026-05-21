/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "memory_source.h"
#include <algorithm>
#include <iostream>

namespace qornix {
namespace rag {

MemorySource::MemorySource(Config config) : config_(std::move(config)) {
}

void MemorySource::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    documents_.clear();
}

std::vector<Document> MemorySource::getDocuments() {
    std::lock_guard<std::mutex> lock(mutex_);
    return documents_;
}

void MemorySource::addDocument(const Document& doc) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!config_.allow_duplicates) {
        // Check for duplicate ID
        if (std::find_if(documents_.begin(), documents_.end(),
            [&doc](const Document& existing) {
                return existing.relative_path == doc.relative_path;
            }) != documents_.end()) {
            std::cerr << "MemorySource: Document with ID already exists: " << doc.relative_path << std::endl;
            return;
        }
    }

    documents_.push_back(doc);
}

void MemorySource::removeDocument(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);

    documents_.erase(
        std::remove_if(documents_.begin(), documents_.end(),
            [&id](const Document& doc) { return doc.relative_path == id; }),
        documents_.end()
    );
}

size_t MemorySource::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return documents_.size();
}

void MemorySource::addDocuments(const std::vector<Document>& docs) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& doc : docs) {
        if (!config_.allow_duplicates) {
            // Check for duplicate ID
            if (std::find_if(documents_.begin(), documents_.end(),
                [&doc](const Document& existing) {
                    return existing.relative_path == doc.relative_path;
                }) != documents_.end()) {
                continue; // Skip duplicates
            }
        }
        documents_.push_back(doc);
    }
}

void MemorySource::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    documents_.clear();
}

bool MemorySource::contains(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find_if(documents_.begin(), documents_.end(),
        [&id](const Document& doc) { return doc.relative_path == id; })
           != documents_.end();
}

std::optional<Document> MemorySource::getDocument(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(documents_.begin(), documents_.end(),
        [&id](const Document& doc) { return doc.relative_path == id; });
    if (it != documents_.end()) {
        return *it;
    }
    return std::nullopt;
}

void MemorySource::setAllowDuplicates(bool allow) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.allow_duplicates = allow;
}

} // namespace rag
} // namespace qornix
