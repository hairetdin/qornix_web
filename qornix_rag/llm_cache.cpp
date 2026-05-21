/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "llm_cache.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

// ============================================================================
// Memory cache implementation
// ============================================================================

MemoryCache::MemoryCache(const CacheConfig& config)
    : max_size_(config.max_size) {
    stats_.max_size = max_size_;
}

std::optional<CacheEntry> MemoryCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) {
        stats_.misses++;
        return std::nullopt;
    }

    // Check if entry is expired
    if (it->second->entry.is_expired()) {
        lru_list_.erase(it->second);
        map_.erase(it);
        stats_.size--;
        stats_.expired++;
        stats_.misses++;
        return std::nullopt;
    }

    // Move to front (most recently used)
    touch(it->second);
    stats_.hits++;
    return it->second->entry;
}

void MemoryCache::put(const std::string& key, const CacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
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

    // Insert new entry at front
    lru_list_.emplace_front(key, entry);
    map_[key] = lru_list_.begin();
    stats_.size++;
}

void MemoryCache::invalidate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it != map_.end()) {
        lru_list_.erase(it->second);
        map_.erase(it);
        stats_.size--;
    }
}

void MemoryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    lru_list_.clear();
    map_.clear();
    stats_.size = 0;
}

CacheStats MemoryCache::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

// ============================================================================
// Redis cache implementation (stub — fallback to memory)
//
// Redis support requires hiredis library.
// If hiredis is not found, this class returns is_available()=false
// and create_cache() falls back to MemoryCache.
// ============================================================================

struct RedisCache::RedisConnection {
    // Placeholder — actual implementation would use hiredis
    // redisContext* ctx = nullptr;
    std::string host;
    int port = 0;
};

RedisCache::RedisCache(const CacheConfig& config)
    : config_(config) {
    stats_.max_size = config_.max_size;
    // Connect asynchronously — will be attempted on first get/put
}

RedisCache::~RedisCache() = default;

bool RedisCache::connect() {
    // TODO: Implement Redis connection using hiredis
    // For now, return false to trigger fallback
    std::cerr << "[RedisCache] Redis backend not yet implemented — "
              << "install hiredis for Redis cache support" << std::endl;
    return false;
}

std::string RedisCache::redis_command(const std::string& cmd) {
    (void)cmd;
    return "";
}

std::optional<CacheEntry> RedisCache::parse_redis_value(const std::string& raw) {
    (void)raw;
    return std::nullopt;
}

std::optional<CacheEntry> RedisCache::get(const std::string& key) {
    if (!connected_.load()) {
        if (!connect()) {
            return std::nullopt;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    stats_.misses++;
    // TODO: Implement Redis GET
    return std::nullopt;
}

void RedisCache::put(const std::string& key, const CacheEntry& entry) {
    if (!connected_.load()) {
        if (!connect()) {
            return;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // TODO: Implement Redis SET with TTL
}

void RedisCache::invalidate(const std::string& key) {
    if (!connected_.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // TODO: Implement Redis DEL
}

void RedisCache::clear() {
    if (!connected_.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // TODO: Implement Redis FLUSHDB
}

CacheStats RedisCache::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

// ============================================================================
// Cache factory
// ============================================================================

std::shared_ptr<ICache> create_cache(const CacheConfig& config) {
    if (!config.enabled) {
        return nullptr;
    }

    if (config.backend == "redis") {
        // Try Redis first
        auto redis = std::make_shared<RedisCache>(config);
        if (redis->is_available()) {
            std::cout << "  ✅ Cache: Redis backend connected" << std::endl;
            return redis;
        }
        std::cerr << "  ⚠️  Redis backend unavailable, falling back to memory cache" << std::endl;
    }

    // Default: memory cache
    auto memory = std::make_shared<MemoryCache>(config);
    std::cout << "  ✅ Cache: Memory backend (max=" << config.max_size
              << ", ttl=" << std::chrono::duration_cast<std::chrono::minutes>(config.ttl).count()
              << " min)" << std::endl;
    return memory;
}

// ============================================================================
// Hashing utilities
// ============================================================================

size_t hash_cache_key(const std::string& question,
                      const std::string& context,
                      const std::string& model,
                      float temperature) {
    // Combine all inputs into a single hash
    std::hash<std::string> string_hash;
    std::hash<float> float_hash;

    size_t h1 = string_hash(question);
    size_t h2 = string_hash(context);
    size_t h3 = string_hash(model);
    size_t h4 = float_hash(temperature);

    // Combine using boost::hash_combine style
    h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
    h1 ^= h3 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
    h1 ^= h4 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);

    return h1;
}

std::string make_cache_key(const std::string& question,
                           const std::string& context,
                           const std::string& model,
                           float temperature) {
    // Create a hex-encoded cache key for better readability in Redis
    size_t hash = hash_cache_key(question, context, model, temperature);

    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}
