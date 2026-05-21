/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for AnalyticsService:
 * - Basic service creation
 * - Log search queries
 * - Get report
 * - Missing answers detection
 * - Knowledge gaps
 * - Top queries
 * - Prune log
 * - Export to JSON
 * - Thread safety
 */

#include "../analytics_service.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sstream>

// Test helpers
#define TEST(name) void name()
#define RUN_TEST(name) do { \
    std::cout << "Running " << #name << "... "; \
    name(); \
    std::cout << "PASSED" << std::endl; \
    test_count++; \
} while(0)

#define ASSERT_EQ(expected, actual, msg) do { \
    if ((expected) != (actual)) { \
        std::cerr << "FAILED: " << msg << " — expected " << (expected) << ", got " << (actual) << std::endl; \
        fail_count++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(condition, msg) do { \
    if (!(condition)) { \
        std::cerr << "FAILED: " << msg << std::endl; \
        fail_count++; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(condition, msg) do { \
    if (condition) { \
        std::cerr << "FAILED: " << msg << std::endl; \
        fail_count++; \
        return; \
    } \
} while(0)

#define ASSERT_GT(value, threshold, msg) do { \
    if ((value) <= (threshold)) { \
        std::cerr << "FAILED: " << msg << " — expected > " << (threshold) << ", got " << (value) << std::endl; \
        fail_count++; \
        return; \
    } \
} while(0)

static int test_count = 0;
static int fail_count = 0;

// ============================================
// AnalyticsService Basic Tests
// ============================================

TEST(analytics_basic) {
    AnalyticsService::Config config;
    config.max_log_entries = 10000;

    auto service = std::make_shared<AnalyticsService>(config);
    ASSERT_TRUE(service != nullptr, "AnalyticsService created");
}

TEST(analytics_config_change) {
    AnalyticsService service;

    auto config = service.getConfig();
    ASSERT_EQ(100000, config.max_log_entries, "Default max_log_entries");

    config.max_log_entries = 5000;
    service.setConfig(config);

    auto new_config = service.getConfig();
    ASSERT_EQ(5000, new_config.max_log_entries, "Config updated");
}

// ============================================
// Search Logging Tests
// ============================================

TEST(analytics_log_search) {
    AnalyticsService service;

    AnalyticsService::SearchQuery query;
    query.query = "How to build project?";
    query.timestamp = std::chrono::system_clock::now();
    query.result_count = 5;
    query.has_answer = true;
    query.response_time_ms = 150;
    query.client_ip = "127.0.0.1";
    query.top_result_path = "README.md";

    service.logSearch(query);

    auto log = service.getSearchLog(10);
    ASSERT_EQ(1, log.size(), "One search logged");
    ASSERT_STR_EQ("How to build project?", log[0].query, "Query matches");
}

TEST(analytics_multiple_searches) {
    AnalyticsService service;

    // Log 10 searches
    for (int i = 0; i < 10; ++i) {
        AnalyticsService::SearchQuery query;
        query.query = "Question " + std::to_string(i) + "?";
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = i;
        query.has_answer = (i > 0);
        query.response_time_ms = 100 + i * 10;
        service.logSearch(query);
    }

    auto log = service.getSearchLog(20);
    ASSERT_EQ(10, log.size(), "10 searches logged");
}

// ============================================
// Report Tests
// ============================================

TEST(analytics_get_report) {
    AnalyticsService service;

    // Log some searches
    for (int i = 0; i < 5; ++i) {
        AnalyticsService::SearchQuery query;
        query.query = "Test query";
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = 3;
        query.has_answer = true;
        query.response_time_ms = 100;
        service.logSearch(query);
    }

    // Log searches without answers
    for (int i = 0; i < 2; ++i) {
        AnalyticsService::SearchQuery query;
        query.query = "Missing answer " + std::to_string(i);
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = 0;
        query.has_answer = false;
        query.response_time_ms = 50;
        service.logSearch(query);
    }

    auto report = service.getRecentReport();
    ASSERT_EQ(7, report.total_queries, "Total queries is 7");
    ASSERT_EQ(5, report.queries_with_results, "5 queries with results");
    ASSERT_EQ(2, report.queries_without_results, "2 queries without results");
    ASSERT_GT(report.answer_rate_percent, 0.0, "Answer rate > 0");
}

TEST(analytics_top_queries) {
    AnalyticsService service;

    // Log same query multiple times
    for (int i = 0; i < 10; ++i) {
        AnalyticsService::SearchQuery query;
        query.query = "Popular question";
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = 3;
        query.has_answer = true;
        query.response_time_ms = 100;
        service.logSearch(query);
    }

    // Log different query once
    {
        AnalyticsService::SearchQuery query;
        query.query = "Rare question";
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = 1;
        query.has_answer = true;
        query.response_time_ms = 200;
        service.logSearch(query);
    }

    auto report = service.getRecentReport();
    ASSERT_GT(report.top_queries.size(), 0, "Top queries not empty");
    ASSERT_STR_EQ("Popular question", report.top_queries[0].query, "Most popular query is first");
}

// ============================================
// Missing Answers Tests
// ============================================

TEST(analytics_missing_detection) {
    AnalyticsService service;

    // Log searches without answers
    for (int i = 0; i < 3; ++i) {
        AnalyticsService::SearchQuery query;
        query.query = "Missing answer " + std::to_string(i);
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = 0;
        query.has_answer = false;
        query.response_time_ms = 50;
        service.logSearch(query);
    }

    auto missing = service.getMissingAnswers();
    ASSERT_GT(missing.size(), 0, "Missing answers detected");
}

TEST(analytics_knowledge_gaps) {
    AnalyticsService service;

    // Log same missing query multiple times
    for (int i = 0; i < 5; ++i) {
        AnalyticsService::SearchQuery query;
        query.query = "How to fix critical bug?";
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = 0;
        query.has_answer = false;
        query.response_time_ms = 30;
        service.logSearch(query);
    }

    auto gaps = service.getKnowledgeGaps(3);  // min_search_count = 3
    ASSERT_GT(gaps.size(), 0, "Knowledge gaps detected");
}

// ============================================
// Export Tests
// ============================================

TEST(analytics_export_json) {
    AnalyticsService service;

    // Log some searches
    for (int i = 0; i < 3; ++i) {
        AnalyticsService::SearchQuery query;
        query.query = "Export test";
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = 2;
        query.has_answer = true;
        query.response_time_ms = 100;
        service.logSearch(query);
    }

    auto report = service.getRecentReport();
    auto json = service.exportToJson(report);

    ASSERT_TRUE(json.length() > 0, "JSON export not empty");
    ASSERT_TRUE(json.find("total_queries") != std::string::npos, "JSON contains total_queries");
}

// ============================================
// Prune Log Tests
// ============================================

TEST(analytics_prune_log) {
    AnalyticsService service;

    // Log some searches
    for (int i = 0; i < 5; ++i) {
        AnalyticsService::SearchQuery query;
        query.query = "Prune test " + std::to_string(i);
        query.timestamp = std::chrono::system_clock::now();
        query.result_count = 1;
        query.has_answer = true;
        query.response_time_ms = 50;
        service.logSearch(query);
    }

    auto before = service.getSearchLog(100);
    ASSERT_EQ(5, before.size(), "5 entries before prune");

    service.pruneLog(30);  // Keep last 30 days

    auto after = service.getSearchLog(100);
    // Entries should still be there (they were just logged)
    ASSERT_EQ(5, after.size(), "5 entries after prune (recent)");
}

// ============================================
// Thread Safety Tests
// ============================================

TEST(analytics_thread_safety) {
    AnalyticsService service;

    const int num_threads = 4;
    const int queries_per_thread = 50;

    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&service, t, queries_per_thread]() {
            for (int i = 0; i < queries_per_thread; ++i) {
                AnalyticsService::SearchQuery query;
                query.query = "Thread " + std::to_string(t) + " query " + std::to_string(i);
                query.timestamp = std::chrono::system_clock::now();
                query.result_count = 1;
                query.has_answer = true;
                query.response_time_ms = 100;
                service.logSearch(query);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto log = service.getSearchLog(1000);
    ASSERT_EQ(num_threads * queries_per_thread, static_cast<int>(log.size()),
              "All queries logged thread-safely");
}

// ============================================
// Main
// ============================================

int main() {
    std::cout << "=== AnalyticsService Tests ===" << std::endl;

    RUN_TEST(analytics_basic);
    RUN_TEST(analytics_config_change);
    RUN_TEST(analytics_log_search);
    RUN_TEST(analytics_multiple_searches);
    RUN_TEST(analytics_get_report);
    RUN_TEST(analytics_top_queries);
    RUN_TEST(analytics_missing_detection);
    RUN_TEST(analytics_knowledge_gaps);
    RUN_TEST(analytics_export_json);
    RUN_TEST(analytics_prune_log);
    RUN_TEST(analytics_thread_safety);

    std::cout << "\n=== Results: " << test_count << " tests, " << fail_count << " failures ===" << std::endl;
    return fail_count > 0 ? 1 : 0;
}
