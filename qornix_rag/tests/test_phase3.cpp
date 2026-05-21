/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Phase 3 Tests: Cache and Rate Limiting
 *
 * Tests for:
 *   - MemoryCache (LRU eviction, TTL expiration, thread safety)
 *   - RateLimiter (sliding window, per-IP limits, whitelist)
 *   - Cache factory (auto backend selection)
 *   - Cache key generation
 */

#include "llm_cache.h"
#include "rate_limiter.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>

// ============================================================================
// Test helpers
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(expected, actual, msg) \
    do { \
        if ((expected) != (actual)) { \
            std::cerr << "❌ ASSERTION FAILED: " << msg \
                      << " (expected: " << (expected) << ", got: " << (actual) << ")" << std::endl; \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define ASSERT_TRUE(condition, msg) \
    do { \
        if (!(condition)) { \
            std::cerr << "❌ ASSERTION FAILED: " << msg << std::endl; \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define ASSERT_FALSE(condition, msg) \
    do { \
        if ((condition)) { \
            std::cerr << "❌ ASSERTION FAILED: " << msg << std::endl; \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

// ============================================================================
// Test 1: MemoryCache basic operations
// ============================================================================

void test_memory_cache_basic() {
    std::cout << "\n🧪 Test 1: MemoryCache basic operations" << std::endl;

    CacheConfig config;
    config.enabled = true;
    config.max_size = 100;
    config.ttl = std::chrono::hours(1);

    MemoryCache cache(config);

    // Test put and get
    CacheEntry entry;
    entry.answer = "Test answer";
    entry.tokens_used = 10;
    entry.ttl = std::chrono::hours(1);
    entry.created_at = std::chrono::steady_clock::now();

    cache.put("key1", entry);

    auto result = cache.get("key1");
    ASSERT_TRUE(result.has_value(), "Cache get should return value");
    ASSERT_EQ("Test answer", result->answer, "Answer should match");
    ASSERT_EQ(10u, result->tokens_used, "Tokens should match");

    // Test cache miss
    auto miss = cache.get("nonexistent");
    ASSERT_FALSE(miss.has_value(), "Cache miss should return nullopt");

    // Test stats
    auto stats = cache.get_stats();
    ASSERT_EQ(1u, stats.hits, "Should have 1 hit");
    ASSERT_EQ(1u, stats.misses, "Should have 1 miss");
    ASSERT_EQ(1u, stats.size, "Cache should have 1 entry");
}

// ============================================================================
// Test 2: MemoryCache LRU eviction
// ============================================================================

void test_memory_cache_lru() {
    std::cout << "\n🧪 Test 2: MemoryCache LRU eviction" << std::endl;

    CacheConfig config;
    config.enabled = true;
    config.max_size = 3;
    config.ttl = std::chrono::hours(1);

    MemoryCache cache(config);

    // Fill cache
    CacheEntry e1, e2, e3;
    e1.answer = "Answer 1";
    e2.answer = "Answer 2";
    e3.answer = "Answer 3";
    e1.created_at = e2.created_at = e3.created_at = std::chrono::steady_clock::now();
    e1.ttl = e2.ttl = e3.ttl = std::chrono::hours(1);

    cache.put("key1", e1);
    cache.put("key2", e2);
    cache.put("key3", e3);

    ASSERT_EQ(3u, cache.get_stats().size, "Cache should have 3 entries");

    // Access key1 to make it recently used
    cache.get("key1");

    // Add key4 — should evict key2 (least recently used)
    CacheEntry e4;
    e4.answer = "Answer 4";
    e4.created_at = std::chrono::steady_clock::now();
    e4.ttl = std::chrono::hours(1);
    cache.put("key4", e4);

    ASSERT_EQ(3u, cache.get_stats().size, "Cache should still have 3 entries");
    ASSERT_EQ(1u, cache.get_stats().evictions, "Should have 1 eviction");

    // key2 should be evicted
    auto miss = cache.get("key2");
    ASSERT_FALSE(miss.has_value(), "key2 should be evicted");

    // key1, key3, key4 should still exist
    ASSERT_TRUE(cache.get("key1").has_value(), "key1 should exist (recently accessed)");
    ASSERT_TRUE(cache.get("key3").has_value(), "key3 should exist");
    ASSERT_TRUE(cache.get("key4").has_value(), "key4 should exist");
}

// ============================================================================
// Test 3: MemoryCache TTL expiration
// ============================================================================

void test_memory_cache_ttl() {
    std::cout << "\n🧪 Test 3: MemoryCache TTL expiration" << std::endl;

    CacheConfig config;
    config.enabled = true;
    config.max_size = 100;
    config.ttl = std::chrono::seconds(1); // 1 second TTL

    MemoryCache cache(config);

    CacheEntry entry;
    entry.answer = "Temporary answer";
    entry.tokens_used = 5;
    entry.ttl = std::chrono::seconds(1);
    entry.created_at = std::chrono::steady_clock::now();

    cache.put("temp_key", entry);

    // Should be available immediately
    ASSERT_TRUE(cache.get("temp_key").has_value(), "Entry should exist immediately after put");

    // Wait for TTL to expire
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Should be expired now
    auto expired = cache.get("temp_key");
    ASSERT_FALSE(expired.has_value(), "Entry should be expired after TTL");
    ASSERT_EQ(1u, cache.get_stats().expired, "Should have 1 expired entry");
}

// ============================================================================
// Test 4: MemoryCache invalidate and clear
// ============================================================================

void test_memory_cache_invalidate() {
    std::cout << "\n🧪 Test 4: MemoryCache invalidate and clear" << std::endl;

    CacheConfig config;
    config.enabled = true;
    config.max_size = 100;
    config.ttl = std::chrono::hours(1);

    MemoryCache cache(config);

    CacheEntry e1, e2;
    e1.answer = "Answer 1";
    e2.answer = "Answer 2";
    e1.created_at = e2.created_at = std::chrono::steady_clock::now();
    e1.ttl = e2.ttl = std::chrono::hours(1);

    cache.put("key1", e1);
    cache.put("key2", e2);

    ASSERT_EQ(2u, cache.get_stats().size, "Cache should have 2 entries");

    // Invalidate one key
    cache.invalidate("key1");
    ASSERT_EQ(1u, cache.get_stats().size, "Cache should have 1 entry after invalidate");
    ASSERT_FALSE(cache.get("key1").has_value(), "key1 should be invalidated");
    ASSERT_TRUE(cache.get("key2").has_value(), "key2 should still exist");

    // Clear all
    cache.clear();
    ASSERT_EQ(0u, cache.get_stats().size, "Cache should be empty after clear");
    ASSERT_FALSE(cache.get("key2").has_value(), "key2 should be gone after clear");
}

// ============================================================================
// Test 5: Cache key generation
// ============================================================================

void test_cache_key_generation() {
    std::cout << "\n🧪 Test 5: Cache key generation" << std::endl;

    std::string key1 = make_cache_key("What is DI?", "context1", "llama3", 0.7f);
    std::string key2 = make_cache_key("What is DI?", "context2", "llama3", 0.7f);
    std::string key3 = make_cache_key("What is DI?", "context1", "llama3", 0.7f);

    ASSERT_TRUE(!key1.empty(), "Cache key should not be empty");
    ASSERT_TRUE(key1 == key3, "Same inputs should produce same key");
    ASSERT_TRUE(key1 != key2, "Different context should produce different key");

    size_t hash1 = hash_cache_key("question", "context", "model", 0.7f);
    size_t hash2 = hash_cache_key("question", "context", "model", 0.7f);
    size_t hash3 = hash_cache_key("different", "context", "model", 0.7f);

    ASSERT_EQ(hash1, hash2, "Same inputs should produce same hash");
    ASSERT_TRUE(hash1 != hash3, "Different question should produce different hash");
}

// ============================================================================
// Test 6: RateLimiter basic operations
// ============================================================================

void test_rate_limiter_basic() {
    std::cout << "\n🧪 Test 6: RateLimiter basic operations" << std::endl;

    RateLimiterConfig config;
    config.enabled = true;
    config.max_requests_per_second = 5;
    config.max_requests_per_minute = 100;
    config.per_ip_limit = false; // Disable per-IP for this test

    RateLimiter limiter(config);

    // Should allow first 5 requests (per second limit)
    for (int i = 0; i < 5; i++) {
        auto result = limiter.allow_request("192.168.1.1");
        ASSERT_FALSE(result.has_value(), "Request should be allowed");
    }

    // 6th request should be rejected
    auto rejected = limiter.allow_request("192.168.1.1");
    ASSERT_TRUE(rejected.has_value(), "6th request should be rejected");
    ASSERT_TRUE(rejected->find("per second") != std::string::npos,
                "Rejection reason should mention per second");

    // Check stats
    auto stats = limiter.get_stats();
    ASSERT_EQ(5u, stats.allowed, "Should have 5 allowed requests");
    ASSERT_EQ(1u, stats.rejected, "Should have 1 rejected request");
}

// ============================================================================
// Test 7: RateLimiter per-IP limits
// ============================================================================

void test_rate_limiter_per_ip() {
    std::cout << "\n🧪 Test 7: RateLimiter per-IP limits" << std::endl;

    RateLimiterConfig config;
    config.enabled = true;
    config.max_requests_per_second = 100; // High global limit
    config.max_requests_per_minute = 1000;
    config.per_ip_limit = true;
    config.max_requests_per_second_per_ip = 3;
    config.max_requests_per_minute_per_ip = 50;

    RateLimiter limiter(config);

    // IP1: should allow 3 requests
    for (int i = 0; i < 3; i++) {
        auto result = limiter.allow_request("10.0.0.1");
        ASSERT_FALSE(result.has_value(), "IP1 request should be allowed");
    }

    // IP1: 4th request should be rejected
    auto rejected_ip1 = limiter.allow_request("10.0.0.1");
    ASSERT_TRUE(rejected_ip1.has_value(), "IP1 4th request should be rejected");

    // IP2: should still be allowed (separate limit)
    auto allowed_ip2 = limiter.allow_request("10.0.0.2");
    ASSERT_FALSE(allowed_ip2.has_value(), "IP2 request should be allowed");

    // Check stats
    auto stats = limiter.get_stats();
    ASSERT_EQ(4u, stats.allowed, "Should have 4 allowed requests (3 from IP1, 1 from IP2)");
    ASSERT_EQ(1u, stats.rejected, "Should have 1 rejected request");
}

// ============================================================================
// Test 8: RateLimiter whitelist
// ============================================================================

void test_rate_limiter_whitelist() {
    std::cout << "\n🧪 Test 8: RateLimiter whitelist" << std::endl;

    RateLimiterConfig config;
    config.enabled = true;
    config.max_requests_per_second = 2; // Very low limit
    config.max_requests_per_minute = 10;
    config.whitelist = {"127.0.0.1", "192.168.1.100"};

    RateLimiter limiter(config);

    // Whitelisted IP should bypass rate limit
    for (int i = 0; i < 10; i++) {
        auto result = limiter.allow_request("127.0.0.1");
        ASSERT_FALSE(result.has_value(), "Whitelisted IP should bypass rate limit");
    }

    // Non-whitelisted IP should be limited
    for (int i = 0; i < 2; i++) {
        auto result = limiter.allow_request("10.0.0.1");
        ASSERT_FALSE(result.has_value(), "Non-whitelisted IP should be allowed initially");
    }
    auto rejected = limiter.allow_request("10.0.0.1");
    ASSERT_TRUE(rejected.has_value(), "Non-whitelisted IP should be rate limited");

    // Check stats
    auto stats = limiter.get_stats();
    ASSERT_EQ(12u, stats.allowed, "Should have 12 allowed (10 whitelisted + 2 non-whitelisted)");
    ASSERT_EQ(1u, stats.rejected, "Should have 1 rejected");
    ASSERT_EQ(10u, stats.whitelisted, "Should have 10 whitelisted requests");
}

// ============================================================================
// Test 9: RateLimiter disabled
// ============================================================================

void test_rate_limiter_disabled() {
    std::cout << "\n🧪 Test 9: RateLimiter disabled" << std::endl;

    RateLimiterConfig config;
    config.enabled = false; // Disabled

    RateLimiter limiter(config);

    ASSERT_FALSE(limiter.is_available(), "Rate limiter should not be available when disabled");

    // Should always allow when disabled
    for (int i = 0; i < 100; i++) {
        auto result = limiter.allow_request("10.0.0.1");
        ASSERT_FALSE(result.has_value(), "Should always allow when disabled");
    }

    // When disabled, stats track allowed but not rejected
    auto stats = limiter.get_stats();
    ASSERT_EQ(0u, stats.rejected, "Should have 0 rejected when disabled");
}

// ============================================================================
// Test 10: Cache factory
// ============================================================================

void test_cache_factory() {
    std::cout << "\n🧪 Test 10: Cache factory" << std::endl;

    // Disabled cache
    CacheConfig disabled_config;
    disabled_config.enabled = false;
    auto disabled_cache = create_cache(disabled_config);
    ASSERT_FALSE(disabled_cache, "Disabled cache should return nullptr");

    // Memory cache (default)
    CacheConfig memory_config;
    memory_config.enabled = true;
    memory_config.backend = "memory";
    memory_config.max_size = 100;
    memory_config.ttl = std::chrono::hours(1);

    auto memory_cache = create_cache(memory_config);
    ASSERT_TRUE(memory_cache != nullptr, "Memory cache should be created");
    ASSERT_TRUE(memory_cache->is_available(), "Memory cache should be available");

    // Test that memory cache works
    CacheEntry entry;
    entry.answer = "Factory test";
    entry.ttl = std::chrono::hours(1);
    entry.created_at = std::chrono::steady_clock::now();

    memory_cache->put("factory_key", entry);
    auto result = memory_cache->get("factory_key");
    ASSERT_TRUE(result.has_value(), "Factory-created cache should work");
    ASSERT_EQ("Factory test", result->answer, "Answer should match");
}

// ============================================================================
// Test 11: MemoryCache thread safety
// ============================================================================

void test_memory_cache_thread_safety() {
    std::cout << "\n🧪 Test 11: MemoryCache thread safety" << std::endl;

    CacheConfig config;
    config.enabled = true;
    config.max_size = 10000;
    config.ttl = std::chrono::hours(1);

    MemoryCache cache(config);

    const int num_threads = 4;
    const int ops_per_thread = 500;

    // Phase 1: Writer threads (each writes unique keys)
    {
        std::vector<std::thread> writers;
        for (int t = 0; t < num_threads; t++) {
            writers.emplace_back([&cache, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; i++) {
                    std::ostringstream key, value;
                    key << "thread" << t << "_key" << i;
                    value << "answer_" << t << "_" << i;

                    CacheEntry entry;
                    entry.answer = value.str();
                    entry.ttl = std::chrono::hours(1);
                    entry.created_at = std::chrono::steady_clock::now();

                    cache.put(key.str(), entry);
                }
            });
        }
        for (auto& t : writers) {
            t.join();
        }
    }

    // Phase 2: Reader threads (each reads the keys written by writer threads)
    {
        std::vector<std::thread> readers;
        for (int t = 0; t < num_threads; t++) {
            readers.emplace_back([&cache, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; i++) {
                    std::ostringstream key;
                    key << "thread" << t << "_key" << i;
                    cache.get(key.str());
                }
            });
        }
        for (auto& t : readers) {
            t.join();
        }
    }

    auto stats = cache.get_stats();
    // 4 threads * 500 ops = 2000 unique keys
    ASSERT_EQ(2000u, stats.size, "Cache should have 2000 entries");
    // 4 reader threads * 500 reads = 2000 hits
    ASSERT_EQ(2000u, stats.hits, "Should have 2000 hits from reader threads");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n╔═══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║        Phase 3 Tests: Cache & Rate Limiting              ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════════╝" << std::endl;

    test_memory_cache_basic();
    test_memory_cache_lru();
    test_memory_cache_ttl();
    test_memory_cache_invalidate();
    test_cache_key_generation();
    test_rate_limiter_basic();
    test_rate_limiter_per_ip();
    test_rate_limiter_whitelist();
    test_rate_limiter_disabled();
    test_cache_factory();
    test_memory_cache_thread_safety();

    std::cout << "\n═══════════════════════════════════════════════════════════" << std::endl;
    std::cout << "📊 Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;

    if (tests_failed == 0) {
        std::cout << "✅ All Phase 3 tests passed!" << std::endl;
    } else {
        std::cout << "❌ Some tests failed!" << std::endl;
    }
    std::cout << "═══════════════════════════════════════════════════════════" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
