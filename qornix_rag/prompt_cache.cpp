/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "prompt_cache.h"
#include <functional>
#include <sstream>
#include <iomanip>

// ============================================================================
// MemoryPromptCache implementation
// ============================================================================

MemoryPromptCache::MemoryPromptCache(const PromptCacheConfig& config)
    : max_size_(config.max_size) {
}

std::optional<PromptCacheEntry> MemoryPromptCache::get(const std::string& question) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(question);
    if (it == map_.end()) {
        stats_.misses++;
        return std::nullopt;
    }

    auto list_it = it->second;
    if (list_it->entry.is_expired()) {
        // Remove expired entry
        map_.erase(it);
        lru_list_.erase(list_it);
        stats_.size--;
        stats_.expired++;
        stats_.misses++;
        return std::nullopt;
    }

    // Move to front (most recently used)
    touch(list_it);
    stats_.hits++;
    return list_it->entry;
}

void MemoryPromptCache::put(const std::string& question, const PromptCacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(question);
    if (it != map_.end()) {
        // Update existing entry
        it->second->entry = entry;
        touch(it->second);
        return;
    }

    // Evict if at capacity
    while (stats_.size >= max_size_) {
        evict_lru();
    }

    // Insert new entry
    lru_list_.emplace_front(question, entry);
    map_[question] = lru_list_.begin();
    stats_.size++;
}

void MemoryPromptCache::invalidate(const std::string& question) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(question);
    if (it != map_.end()) {
        lru_list_.erase(it->second);
        map_.erase(it);
        stats_.size--;
    }
}

void MemoryPromptCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lru_list_.clear();
    map_.clear();
    stats_.size = 0;
}

PromptCacheStats MemoryPromptCache::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

// ============================================================================
// Factory
// ============================================================================

std::shared_ptr<IPromptCache> create_prompt_cache(const PromptCacheConfig& config) {
    if (!config.enabled) {
        return nullptr;
    }
    return std::make_shared<MemoryPromptCache>(config);
}

// ============================================================================
// Hashing utilities
// ============================================================================

size_t hash_prompt(const std::string& question) {
    return std::hash<std::string>{}(question);
}

std::string make_prompt_cache_key(const std::string& question) {
    return "prompt:" + std::to_string(hash_prompt(question));
}
