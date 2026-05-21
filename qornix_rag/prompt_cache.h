/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Prompt Cache — Phase 3
 *
 * Caches the results of prompt building (context retrieval) to avoid
 * redundant RAG search operations for repeated questions.
 *
 * Unlike response cache (which caches LLM answers), prompt cache caches
 * the intermediate search results — the context strings and source documents.
 *
 * This is useful when:
 *   - Same question is asked multiple times
 *   - Context retrieval is expensive (large codebase)
 *   - LLM is slow but search results are stable
 *
 * Cache key = hash(question)
 * Cache entry = {context_string, context_array, sources, search_timestamp}
 */

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>
#include <atomic>
#include <memory>
#include <list>
#include <vector>
#include <algorithm>

// ============================================================================
// Prompt cache entry
// ============================================================================

struct PromptCacheEntry {
    std::string context;                    // Plain text context
    std::vector<std::string> sources;       // Source file paths
    std::chrono::steady_clock::time_point cached_at;
    std::chrono::seconds ttl;               // Time-to-live

    bool is_expired() const {
        return std::chrono::steady_clock::now() - cached_at > ttl;
    }
};

// ============================================================================
// Prompt cache statistics
// ============================================================================

struct PromptCacheStats {
    size_t hits = 0;
    size_t misses = 0;
    size_t evictions = 0;
    size_t expired = 0;
    size_t size = 0;
    size_t max_size = 0;

    double hit_rate() const {
        size_t total = hits + misses;
        return total > 0 ? static_cast<double>(hits) / total * 100.0 : 0.0;
    }
};

// ============================================================================
// Prompt cache configuration
// ============================================================================

struct PromptCacheConfig {
    bool enabled = false;
    size_t max_size = 500;                  // Maximum cached prompts
    std::chrono::seconds ttl = std::chrono::minutes(30); // Default TTL (30 min)
};

// ============================================================================
// Prompt cache interface
// ============================================================================

class IPromptCache {
public:
    virtual ~IPromptCache() = default;

    virtual std::optional<PromptCacheEntry> get(const std::string& question) = 0;
    virtual void put(const std::string& question, const PromptCacheEntry& entry) = 0;
    virtual void invalidate(const std::string& question) = 0;
    virtual void clear() = 0;
    virtual PromptCacheStats get_stats() const = 0;
    virtual bool is_available() const = 0;
};

// ============================================================================
// Memory prompt cache (thread-safe LRU)
// ============================================================================

class MemoryPromptCache : public IPromptCache {
private:
    struct Node {
        std::string question;
        PromptCacheEntry entry;

        Node() = default;
        Node(std::string q, PromptCacheEntry e)
            : question(std::move(q)), entry(std::move(e)) {}
    };

    std::list<Node> lru_list_;
    std::unordered_map<std::string, std::list<Node>::iterator> map_;

    mutable std::mutex mutex_;
    size_t max_size_;
    PromptCacheStats stats_;

    void evict_lru() {
        if (lru_list_.empty()) return;
        auto back = std::prev(lru_list_.end());
        map_.erase(back->question);
        lru_list_.pop_back();
        stats_.size--;
        stats_.evictions++;
    }

    void touch(std::list<Node>::iterator it) {
        lru_list_.splice(lru_list_.begin(), lru_list_, it);
    }

public:
    explicit MemoryPromptCache(const PromptCacheConfig& config);
    ~MemoryPromptCache() override = default;

    MemoryPromptCache(const MemoryPromptCache&) = delete;
    MemoryPromptCache& operator=(const MemoryPromptCache&) = delete;

    std::optional<PromptCacheEntry> get(const std::string& question) override;
    void put(const std::string& question, const PromptCacheEntry& entry) override;
    void invalidate(const std::string& question) override;
    void clear() override;
    PromptCacheStats get_stats() const override;
    bool is_available() const override { return true; }
};

// ============================================================================
// Factory
// ============================================================================

std::shared_ptr<IPromptCache> create_prompt_cache(const PromptCacheConfig& config);

// ============================================================================
// Hashing utilities
// ============================================================================

size_t hash_prompt(const std::string& question);
std::string make_prompt_cache_key(const std::string& question);
