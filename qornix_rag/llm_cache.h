/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * LLM Response Cache — Phase 3
 *
 * Thread-safe LRU cache for LLM responses.
 * Supports two backends:
 *   - memory: in-process unordered_map with LRU eviction (default)
 *   - redis: external Redis server (optional, detected at runtime)
 *
 * Cache key = hash(question + context_hash + model + temperature)
 * Cache entry = {answer, tokens_used, created_at, ttl}
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
#include <algorithm>

// ============================================================================
// Cache entry
// ============================================================================

struct CacheEntry {
    std::string answer;
    size_t tokens_used = 0;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::seconds ttl; // time-to-live

    bool is_expired() const {
        return std::chrono::steady_clock::now() - created_at > ttl;
    }
};

// ============================================================================
// Cache statistics
// ============================================================================

struct CacheStats {
    size_t hits = 0;
    size_t misses = 0;
    size_t evictions = 0;
    size_t expired = 0;
    size_t size = 0;       // current number of entries
    size_t max_size = 0;   // maximum capacity

    double hit_rate() const {
        size_t total = hits + misses;
        return total > 0 ? static_cast<double>(hits) / total * 100.0 : 0.0;
    }
};

// ============================================================================
// Cache configuration
// ============================================================================

struct CacheConfig {
    bool enabled = false;
    std::string backend = "memory"; // "memory" or "redis"

    // Memory backend settings
    size_t max_size = 1000;          // maximum number of cached entries
    std::chrono::seconds ttl = std::chrono::hours(1); // default TTL

    // Redis backend settings (optional external backend)
    std::string redis_host = "127.0.0.1";
    int redis_port = 6379;
    int redis_db = 0;
    std::string redis_password;
    std::chrono::seconds redis_ttl = std::chrono::hours(1);

    // Key prefix to avoid collisions
    std::string key_prefix = "qornix_rag:";
};

// ============================================================================
// Cache interface (abstract base)
// ============================================================================

class ICache {
public:
    virtual ~ICache() = default;

    // Get a cached entry, nullopt if not found or expired
    virtual std::optional<CacheEntry> get(const std::string& key) = 0;

    // Store an entry in the cache
    virtual void put(const std::string& key, const CacheEntry& entry) = 0;

    // Remove a specific key
    virtual void invalidate(const std::string& key) = 0;

    // Clear all entries
    virtual void clear() = 0;

    // Get cache statistics
    virtual CacheStats get_stats() const = 0;

    // Check if cache is available
    virtual bool is_available() const = 0;

    // Human-readable backend name for diagnostics
    virtual std::string backend_name() const = 0;
};

// ============================================================================
// Memory cache (thread-safe LRU)
// ============================================================================

class MemoryCache : public ICache {
private:
    struct Node {
        std::string key;
        CacheEntry entry;

        Node() = default;
        Node(std::string k, CacheEntry e)
            : key(std::move(k)), entry(std::move(e)) {}
    };

    // LRU list: most recently used at front
    std::list<Node> lru_list_;
    std::unordered_map<std::string, std::list<Node>::iterator> map_;

    mutable std::mutex mutex_;
    size_t max_size_;
    CacheStats stats_;

    // Evict least recently used entry
    void evict_lru() {
        if (lru_list_.empty()) {
            return;
        }
        auto back = std::prev(lru_list_.end());
        map_.erase(back->key);
        lru_list_.pop_back();
        stats_.size--;
        stats_.evictions++;
    }

    // Move node to front (most recently used)
    void touch(std::list<Node>::iterator it) {
        lru_list_.splice(lru_list_.begin(), lru_list_, it);
    }

public:
    explicit MemoryCache(const CacheConfig& config);
    ~MemoryCache() override = default;

    // Non-copyable
    MemoryCache(const MemoryCache&) = delete;
    MemoryCache& operator=(const MemoryCache&) = delete;

    std::optional<CacheEntry> get(const std::string& key) override;
    void put(const std::string& key, const CacheEntry& entry) override;
    void invalidate(const std::string& key) override;
    void clear() override;
    CacheStats get_stats() const override;
    bool is_available() const override { return true; }
    std::string backend_name() const override { return "memory"; }
};

// ============================================================================
// Redis cache (optional, detected at runtime)
// ============================================================================

class RedisCache : public ICache {
private:
    // Lightweight Redis RESP client over TCP. No hiredis dependency is required.
    // If Redis is unavailable at startup/runtime, create_cache() falls back to memory.
    struct RedisConnection;
    std::unique_ptr<RedisConnection> connection_;
    CacheConfig config_;
    mutable std::mutex mutex_;
    CacheStats stats_;
    std::atomic<bool> connected_{false};

    bool connect();
    std::string redis_command(const std::string& cmd);
    std::optional<CacheEntry> parse_redis_value(const std::string& raw);

public:
    explicit RedisCache(const CacheConfig& config);
    ~RedisCache() override;

    // Non-copyable
    RedisCache(const RedisCache&) = delete;
    RedisCache& operator=(const RedisCache&) = delete;

    std::optional<CacheEntry> get(const std::string& key) override;
    void put(const std::string& key, const CacheEntry& entry) override;
    void invalidate(const std::string& key) override;
    void clear() override;
    CacheStats get_stats() const override;
    bool is_available() const override { return connected_.load(); }
    std::string backend_name() const override { return "redis"; }
};

// ============================================================================
// Cache factory (automatic backend selection)
// ============================================================================

std::shared_ptr<ICache> create_cache(const CacheConfig& config);

// ============================================================================
// Hashing utilities
// ============================================================================

// Simple hash function for cache key generation
size_t hash_cache_key(const std::string& question,
                      const std::string& context,
                      const std::string& model,
                      float temperature);

// Generate a cache key string
std::string make_cache_key(const std::string& question,
                           const std::string& context,
                           const std::string& model,
                           float temperature);
