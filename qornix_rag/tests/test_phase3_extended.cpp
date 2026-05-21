/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Phase 3 Extended Tests: Batch Processing, Prompt Cache, Prometheus Metrics
 *
 * Tests for:
 *   - BatchProcessor (basic, concurrent, timeout, progress callback)
 *   - MemoryPromptCache (basic, LRU, TTL, invalidate)
 *   - PromptCache factory
 *   - Prompt cache key generation
 *   - Prometheus metrics (counters, gauges, summaries, rendering)
 *   - LLMRAGMetrics convenience API
 */

#include "batch_processor.h"
#include "prompt_cache.h"
#include "prometheus_metrics.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <atomic>
#include <algorithm>

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

#define ASSERT_STR_EQ(expected, actual, msg) \
    do { \
        if ((expected) != (actual)) { \
            std::cerr << "❌ ASSERTION FAILED: " << msg \
                      << " (expected: \"" << (expected) << "\", got: \"" << (actual) << "\")" << std::endl; \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

// ============================================================================
// Test 1: BatchProcessor basic
// ============================================================================

void test_batch_processor_basic() {
    std::cout << "\n🧪 Test 1: BatchProcessor basic" << std::endl;

    BatchConfig config;
    config.max_concurrent = 2;
    config.question_timeout_ms = 10000;

    BatchProcessor processor(config);

    std::vector<BatchQuestion> questions = {
        {"Question 1", "Context 1", 5, ""},
        {"Question 2", "Context 2", 5, ""},
        {"Question 3", "Context 3", 5, ""}
    };

    // Simple ask function that just returns a fixed answer
    auto ask_fn = [](const std::string& question, const std::string& /*context*/, const std::string& /*ip*/) -> std::string {
        return "Answer to: " + question;
    };

    auto results = processor.process(questions, ask_fn);

    ASSERT_EQ(3u, results.size(), "Should have 3 results");

    for (size_t i = 0; i < results.size(); i++) {
        ASSERT_TRUE(results[i].success, "Question " + std::to_string(i+1) + " should succeed");
        ASSERT_STR_EQ("Answer to: Question " + std::to_string(i+1), results[i].answer,
                     "Answer should match for question " + std::to_string(i+1));
        ASSERT_TRUE(results[i].response_time_ms >= 0, "Response time should be non-negative");
    }

    auto stats = processor.get_stats();
    ASSERT_EQ(3u, stats.total, "Should have 3 total questions");
    ASSERT_EQ(3u, stats.completed, "Should have 3 completed");
    ASSERT_EQ(0u, stats.failed, "Should have 0 failed");
}

// ============================================================================
// Test 2: BatchProcessor concurrent execution
// ============================================================================

void test_batch_processor_concurrent() {
    std::cout << "\n🧪 Test 2: BatchProcessor concurrent execution" << std::endl;

    BatchConfig config;
    config.max_concurrent = 4;  // Allow 4 concurrent
    config.question_timeout_ms = 30000;

    BatchProcessor processor(config);

    std::atomic<int> concurrent_count{0};
    std::atomic<int> max_concurrent{0};

    std::vector<BatchQuestion> questions;
    for (int i = 0; i < 8; i++) {
        questions.push_back({"Question " + std::to_string(i), "", 5, ""});
    }

    auto ask_fn = [&concurrent_count, &max_concurrent](const std::string&, const std::string&, const std::string&) -> std::string {
        int current = concurrent_count.fetch_add(1) + 1;
        int max = max_concurrent.load();
        while (current > max) {
            if (max_concurrent.compare_exchange_weak(max, current)) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        concurrent_count.fetch_sub(1);
        return "Done";
    };

    auto results = processor.process(questions, ask_fn);

    ASSERT_EQ(8u, results.size(), "Should have 8 results");
    ASSERT_TRUE(max_concurrent.load() <= 4, "Should not exceed max_concurrent=4");
    ASSERT_TRUE(max_concurrent.load() > 0, "Should have some concurrent execution");

    for (const auto& r : results) {
        ASSERT_TRUE(r.success, "All questions should succeed");
    }
}

// ============================================================================
// Test 3: BatchProcessor progress callback
// ============================================================================

void test_batch_processor_progress() {
    std::cout << "\n🧪 Test 3: BatchProcessor progress callback" << std::endl;

    BatchConfig config;
    config.max_concurrent = 2;

    BatchProcessor processor(config);

    std::atomic<size_t> progress_calls{0};
    std::vector<std::pair<size_t, size_t>> progress_log;

    processor.set_progress_callback([&progress_log, &progress_calls](size_t completed, size_t total, size_t /*index*/) {
        progress_calls.fetch_add(1);
        progress_log.push_back({completed, total});
    });

    std::vector<BatchQuestion> questions;
    for (int i = 0; i < 4; i++) {
        questions.push_back({"Question " + std::to_string(i), "", 5, ""});
    }

    auto ask_fn = [](const std::string&, const std::string&, const std::string&) -> std::string {
        return "Answer";
    };

    auto results = processor.process(questions, ask_fn);

    ASSERT_EQ(4u, results.size(), "Should have 4 results");
    ASSERT_TRUE(progress_calls.load() >= 4, "Should have at least 4 progress callbacks");
    ASSERT_TRUE(!progress_log.empty(), "Progress log should not be empty");

    // Last progress call should show all completed
    auto last = progress_log.back();
    ASSERT_EQ(4u, last.second, "Total should be 4");
}

// ============================================================================
// Test 4: BatchProcessor error handling
// ============================================================================

void test_batch_processor_errors() {
    std::cout << "\n🧪 Test 4: BatchProcessor error handling" << std::endl;

    BatchConfig config;
    config.max_concurrent = 2;
    config.stop_on_error = false;

    BatchProcessor processor(config);

    std::vector<BatchQuestion> questions = {
        {"Good question", "", 5, ""},
        {"Bad question", "", 5, ""},
        {"Another good", "", 5, ""}
    };

    std::atomic<int> call_count{0};
    auto ask_fn = [&call_count](const std::string& question, const std::string&, const std::string&) -> std::string {
        if (question.find("Bad") != std::string::npos) {
            throw std::runtime_error("Simulated error");
        }
        call_count.fetch_add(1);
        return "OK";
    };

    auto results = processor.process(questions, ask_fn);

    ASSERT_EQ(3u, results.size(), "Should have 3 results");
    ASSERT_TRUE(results[0].success, "First question should succeed");
    ASSERT_FALSE(results[1].success, "Second question should fail");
    ASSERT_TRUE(results[2].success, "Third question should succeed (stop_on_error=false)");

    auto stats = processor.get_stats();
    ASSERT_EQ(2u, stats.completed, "Should have 2 completed");
    ASSERT_EQ(1u, stats.failed, "Should have 1 failed");
}

// ============================================================================
// Test 5: MemoryPromptCache basic
// ============================================================================

void test_prompt_cache_basic() {
    std::cout << "\n🧪 Test 5: MemoryPromptCache basic" << std::endl;

    PromptCacheConfig config;
    config.enabled = true;
    config.max_size = 100;
    config.ttl = std::chrono::hours(1);

    MemoryPromptCache cache(config);

    PromptCacheEntry entry;
    entry.context = "Context for question";
    entry.sources = {"file1.h", "file2.cpp"};
    entry.ttl = std::chrono::hours(1);
    entry.cached_at = std::chrono::steady_clock::now();

    cache.put("What is DI?", entry);

    auto result = cache.get("What is DI?");
    ASSERT_TRUE(result.has_value(), "Cache should return value");
    ASSERT_STR_EQ("Context for question", result->context, "Context should match");
    ASSERT_EQ(2u, result->sources.size(), "Should have 2 sources");

    auto miss = cache.get("Different question");
    ASSERT_FALSE(miss.has_value(), "Cache miss should return nullopt");

    auto stats = cache.get_stats();
    ASSERT_EQ(1u, stats.hits, "Should have 1 hit");
    ASSERT_EQ(1u, stats.misses, "Should have 1 miss");
}

// ============================================================================
// Test 6: MemoryPromptCache LRU eviction
// ============================================================================

void test_prompt_cache_lru() {
    std::cout << "\n🧪 Test 6: MemoryPromptCache LRU eviction" << std::endl;

    PromptCacheConfig config;
    config.enabled = true;
    config.max_size = 3;
    config.ttl = std::chrono::hours(1);

    MemoryPromptCache cache(config);

    for (int i = 0; i < 3; i++) {
        PromptCacheEntry entry;
        entry.context = "Context " + std::to_string(i);
        entry.ttl = std::chrono::hours(1);
        entry.cached_at = std::chrono::steady_clock::now();
        cache.put("Question " + std::to_string(i), entry);
    }

    ASSERT_EQ(3u, cache.get_stats().size, "Cache should have 3 entries");

    // Access first question to make it recently used
    cache.get("Question 0");

    // Add fourth — should evict Question 1 (least recently used)
    PromptCacheEntry e4;
    e4.context = "Context 4";
    e4.ttl = std::chrono::hours(1);
    e4.cached_at = std::chrono::steady_clock::now();
    cache.put("Question 4", e4);

    ASSERT_EQ(3u, cache.get_stats().size, "Cache should still have 3 entries");
    ASSERT_EQ(1u, cache.get_stats().evictions, "Should have 1 eviction");

    // Question 1 should be evicted
    ASSERT_FALSE(cache.get("Question 1").has_value(), "Question 1 should be evicted");

    // Question 0, 2, 4 should exist
    ASSERT_TRUE(cache.get("Question 0").has_value(), "Question 0 should exist");
    ASSERT_TRUE(cache.get("Question 2").has_value(), "Question 2 should exist");
    ASSERT_TRUE(cache.get("Question 4").has_value(), "Question 4 should exist");
}

// ============================================================================
// Test 7: MemoryPromptCache TTL expiration
// ============================================================================

void test_prompt_cache_ttl() {
    std::cout << "\n🧪 Test 7: MemoryPromptCache TTL expiration" << std::endl;

    PromptCacheConfig config;
    config.enabled = true;
    config.max_size = 100;
    config.ttl = std::chrono::seconds(1);

    MemoryPromptCache cache(config);

    PromptCacheEntry entry;
    entry.context = "Temporary context";
    entry.ttl = std::chrono::seconds(1);
    entry.cached_at = std::chrono::steady_clock::now();

    cache.put("temp_question", entry);

    ASSERT_TRUE(cache.get("temp_question").has_value(), "Entry should exist immediately");

    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto expired = cache.get("temp_question");
    ASSERT_FALSE(expired.has_value(), "Entry should be expired after TTL");
    ASSERT_EQ(1u, cache.get_stats().expired, "Should have 1 expired entry");
}

// ============================================================================
// Test 8: Prompt cache factory
// ============================================================================

void test_prompt_cache_factory() {
    std::cout << "\n🧪 Test 8: Prompt cache factory" << std::endl;

    // Disabled cache
    PromptCacheConfig disabled_config;
    disabled_config.enabled = false;
    auto disabled_cache = create_prompt_cache(disabled_config);
    ASSERT_FALSE(disabled_cache, "Disabled prompt cache should return nullptr");

    // Enabled cache
    PromptCacheConfig enabled_config;
    enabled_config.enabled = true;
    enabled_config.max_size = 100;
    enabled_config.ttl = std::chrono::hours(1);

    auto enabled_cache = create_prompt_cache(enabled_config);
    ASSERT_TRUE(enabled_cache != nullptr, "Enabled prompt cache should be created");
    ASSERT_TRUE(enabled_cache->is_available(), "Prompt cache should be available");

    // Test basic operation
    PromptCacheEntry entry;
    entry.context = "Factory test context";
    entry.ttl = std::chrono::hours(1);
    entry.cached_at = std::chrono::steady_clock::now();

    enabled_cache->put("factory_question", entry);
    auto result = enabled_cache->get("factory_question");
    ASSERT_TRUE(result.has_value(), "Factory-created cache should work");
    ASSERT_STR_EQ("Factory test context", result->context, "Context should match");
}

// ============================================================================
// Test 9: Prompt cache key generation
// ============================================================================

void test_prompt_cache_key_generation() {
    std::cout << "\n🧪 Test 9: Prompt cache key generation" << std::endl;

    std::string key1 = make_prompt_cache_key("What is DI?");
    std::string key2 = make_prompt_cache_key("What is DI?");
    std::string key3 = make_prompt_cache_key("Different question");

    ASSERT_TRUE(key1.find("prompt:") == 0, "Key should start with 'prompt:'");
    ASSERT_TRUE(key1 == key2, "Same question should produce same key");
    ASSERT_TRUE(key1 != key3, "Different question should produce different key");

    size_t hash1 = hash_prompt("question");
    size_t hash2 = hash_prompt("question");
    size_t hash3 = hash_prompt("different");

    ASSERT_EQ(hash1, hash2, "Same input should produce same hash");
    ASSERT_TRUE(hash1 != hash3, "Different input should produce different hash");
}

// ============================================================================
// Test 10: Prometheus counters
// ============================================================================

void test_prometheus_counters() {
    std::cout << "\n🧪 Test 10: Prometheus counters" << std::endl;

    auto registry = std::make_shared<MetricsRegistry>();
    auto counter = registry->get_or_create_counter("test_counter");

    ASSERT_EQ(0.0, counter->get(), "Counter should start at 0");

    counter->inc();
    ASSERT_EQ(1.0, counter->get(), "Counter should be 1 after inc()");

    counter->inc(5.0);
    ASSERT_EQ(6.0, counter->get(), "Counter should be 6 after inc(5)");

    // Render and check format
    std::string rendered = counter->render("test_counter");
    ASSERT_TRUE(rendered.find("# HELP") != std::string::npos, "Should contain HELP line");
    ASSERT_TRUE(rendered.find("# TYPE") != std::string::npos, "Should contain TYPE line");
    ASSERT_TRUE(rendered.find("test_counter 6.0") != std::string::npos, "Should contain value 6.0");
}

// ============================================================================
// Test 11: Prometheus gauges
// ============================================================================

void test_prometheus_gauges() {
    std::cout << "\n🧪 Test 11: Prometheus gauges" << std::endl;

    auto registry = std::make_shared<MetricsRegistry>();
    auto gauge = registry->get_or_create_gauge("test_gauge");

    ASSERT_EQ(0.0, gauge->get(), "Gauge should start at 0");

    gauge->set(42.0);
    ASSERT_EQ(42.0, gauge->get(), "Gauge should be 42 after set(42)");

    gauge->inc(8.0);
    ASSERT_EQ(50.0, gauge->get(), "Gauge should be 50 after inc(8)");

    gauge->dec(10.0);
    ASSERT_EQ(40.0, gauge->get(), "Gauge should be 40 after dec(10)");

    // Render and check format
    std::string rendered = gauge->render("test_gauge");
    ASSERT_TRUE(rendered.find("# TYPE") != std::string::npos, "Should contain TYPE line");
    ASSERT_TRUE(rendered.find("test_gauge 40.0") != std::string::npos, "Should contain value 40.0");
}

// ============================================================================
// Test 12: Prometheus summaries
// ============================================================================

void test_prometheus_summaries() {
    std::cout << "\n🧪 Test 12: Prometheus summaries" << std::endl;

    auto registry = std::make_shared<MetricsRegistry>();
    auto summary = registry->get_or_create_summary("test_summary");

    ASSERT_EQ(0.0, summary->get_count(), "Summary count should start at 0");

    summary->observe(1.0);
    summary->observe(2.0);
    summary->observe(3.0);

    ASSERT_EQ(3.0, summary->get_count(), "Summary count should be 3");
    ASSERT_EQ(6.0, summary->get_sum(), "Summary sum should be 6");
    ASSERT_EQ(2.0, summary->get_avg(), "Summary avg should be 2");

    // Render and check format
    std::string rendered = summary->render("test_summary");
    ASSERT_TRUE(rendered.find("test_summary_sum") != std::string::npos, "Should contain _sum");
    ASSERT_TRUE(rendered.find("test_summary_count") != std::string::npos, "Should contain _count");
}

// ============================================================================
// Test 13: LLMRAGMetrics convenience API
// ============================================================================

void test_llm_rag_metrics() {
    std::cout << "\n🧪 Test 13: LLMRAGMetrics convenience API" << std::endl;

    auto metrics = std::make_shared<LLMRAGMetrics>();

    // Test all convenience methods
    metrics->inc_llm_requests();
    metrics->inc_llm_requests();
    metrics->inc_llm_requests_failed();
    metrics->inc_llm_tokens(256);
    metrics->observe_llm_request_duration_seconds(1.5);
    metrics->inc_cache_hits();
    metrics->inc_cache_misses();
    metrics->set_cache_size(42);
    metrics->inc_rate_limit_rejections();
    metrics->inc_batch_questions(5);
    metrics->inc_batch_completed(4);
    metrics->set_indexed_files(150);
    metrics->set_indexed_lines(25000);

    // Render all metrics
    std::string rendered = metrics->render_all();

    ASSERT_TRUE(rendered.find("qornix_rag_llm_requests_total") != std::string::npos,
                "Should contain llm_requests_total");
    ASSERT_TRUE(rendered.find("qornix_rag_cache_hits_total") != std::string::npos,
                "Should contain cache_hits_total");
    ASSERT_TRUE(rendered.find("qornix_rag_cache_size_gauge") != std::string::npos,
                "Should contain cache_size_gauge");
    ASSERT_TRUE(rendered.find("qornix_rag_indexed_files_count") != std::string::npos,
                "Should contain indexed_files_count");
    ASSERT_TRUE(rendered.find("qornix_rag_indexed_lines_count") != std::string::npos,
                "Should contain indexed_lines_count");
    ASSERT_TRUE(rendered.find("qornix_rag_batch_questions_total") != std::string::npos,
                "Should contain batch_questions_total");
    ASSERT_TRUE(rendered.find("qornix_rag_rate_limit_rejections_total") != std::string::npos,
                "Should contain rate_limit_rejections_total");
    ASSERT_TRUE(rendered.find("qornix_rag_llm_request_duration_seconds") != std::string::npos,
                "Should contain request_duration_seconds");
}

// ============================================================================
// Test 14: MetricsRegistry render_all
// ============================================================================

void test_metrics_registry_render_all() {
    std::cout << "\n🧪 Test 14: MetricsRegistry render_all" << std::endl;

    auto registry = std::make_shared<MetricsRegistry>();

    registry->get_or_create_counter("counter_a")->inc(10);
    registry->get_or_create_gauge("gauge_a")->set(20);
    registry->get_or_create_summary("summary_a")->observe(30);

    std::string rendered = registry->render_all();

    ASSERT_TRUE(rendered.find("counter_a") != std::string::npos, "Should contain counter_a");
    ASSERT_TRUE(rendered.find("gauge_a") != std::string::npos, "Should contain gauge_a");
    ASSERT_TRUE(rendered.find("summary_a") != std::string::npos, "Should contain summary_a");
    ASSERT_TRUE(rendered.find("# TYPE") != std::string::npos, "Should contain TYPE lines");
    ASSERT_TRUE(rendered.find("# HELP") != std::string::npos, "Should contain HELP lines");
}

// ============================================================================
// Test 15: MemoryPromptCache thread safety
// ============================================================================

void test_prompt_cache_thread_safety() {
    std::cout << "\n🧪 Test 15: MemoryPromptCache thread safety" << std::endl;

    PromptCacheConfig config;
    config.enabled = true;
    config.max_size = 10000;
    config.ttl = std::chrono::hours(1);

    MemoryPromptCache cache(config);

    const int num_threads = 4;
    const int ops_per_thread = 500;

    // Writer threads
    {
        std::vector<std::thread> writers;
        for (int t = 0; t < num_threads; t++) {
            writers.emplace_back([&cache, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; i++) {
                    std::ostringstream key;
                    key << "thread" << t << "_q" << i;

                    PromptCacheEntry entry;
                    entry.context = "context_" + std::to_string(t) + "_" + std::to_string(i);
                    entry.ttl = std::chrono::hours(1);
                    entry.cached_at = std::chrono::steady_clock::now();

                    cache.put(key.str(), entry);
                }
            });
        }
        for (auto& t : writers) {
            t.join();
        }
    }

    // Reader threads
    {
        std::vector<std::thread> readers;
        for (int t = 0; t < num_threads; t++) {
            readers.emplace_back([&cache, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; i++) {
                    std::ostringstream key;
                    key << "thread" << t << "_q" << i;
                    cache.get(key.str());
                }
            });
        }
        for (auto& t : readers) {
            t.join();
        }
    }

    auto stats = cache.get_stats();
    ASSERT_EQ(2000u, stats.size, "Cache should have 2000 entries");
    ASSERT_EQ(2000u, stats.hits, "Should have 2000 hits");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n╔═══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║   Phase 3 Extended Tests: Batch, Prompt Cache, Metrics   ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════════╝" << std::endl;

    test_batch_processor_basic();
    test_batch_processor_concurrent();
    test_batch_processor_progress();
    test_batch_processor_errors();
    test_prompt_cache_basic();
    test_prompt_cache_lru();
    test_prompt_cache_ttl();
    test_prompt_cache_factory();
    test_prompt_cache_key_generation();
    test_prometheus_counters();
    test_prometheus_gauges();
    test_prometheus_summaries();
    test_llm_rag_metrics();
    test_metrics_registry_render_all();
    test_prompt_cache_thread_safety();

    std::cout << "\n═══════════════════════════════════════════════════════════" << std::endl;
    std::cout << "📊 Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;

    if (tests_failed == 0) {
        std::cout << "✅ All Phase 3 extended tests passed!" << std::endl;
    } else {
        std::cout << "❌ Some tests failed!" << std::endl;
    }
    std::cout << "═══════════════════════════════════════════════════════════" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
