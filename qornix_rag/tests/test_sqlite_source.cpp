/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for SQLiteSource:
 * - Basic creation and initialization
 * - Add/find/update/delete QA pairs
 * - Search by category and question
 * - Persistence (database file creation)
 * - Pagination
 * - Migration
 * - Hash uniqueness
 */

#include "../sqlite_source.h"
#include "../qa_source.h"
#include "../data_source.h"
#include "../core.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
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

#define ASSERT_NOT_NULL(ptr, msg) do { \
    if ((ptr) == nullptr) { \
        std::cerr << "FAILED: " << msg << " — pointer is null" << std::endl; \
        fail_count++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual, msg) do { \
    if ((expected) != (actual)) { \
        std::cerr << "FAILED: " << msg << " — expected '" << (expected) << "', got '" << (actual) << "'" << std::endl; \
        fail_count++; \
        return; \
    } \
} while(0)

static int test_count = 0;
static int fail_count = 0;

// Clean up test database
static void cleanup_test_db(const std::string& path) {
    std::filesystem::remove(path);
}

// ============================================
// SQLiteSource Basic Tests
// ============================================

TEST(sqlite_source_basic) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_basic.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_sqlite";
    config.name = "Test SQLite Source";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    ASSERT_NOT_NULL(source, "SQLiteSource created");

    bool init = source->initialize();
    ASSERT_TRUE(init, "SQLiteSource initialized");

    ASSERT_TRUE(source->getType() == DataSourceType::DATABASE, "DataSource type is DATABASE");
    ASSERT_EQ(0, source->count(), "Initial count is 0");
    ASSERT_STR_EQ("test_sqlite", source->getId(), "Source ID matches");
    ASSERT_STR_EQ("Test SQLite Source", source->getName(), "Source name matches");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_add_pair) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_add.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_add";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    bool added = source->addQAPair(
        "qa_001",
        "What is C++?",
        "C++ is a general-purpose programming language.",
        "programming",
        "[\"cpp\", \"cplusplus\"]",
        "{\"version\": \"17\"}"
    );
    ASSERT_TRUE(added, "QA pair added successfully");
    ASSERT_EQ(1, source->count(), "Count is 1 after add");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_find_pair) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_find.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_find";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    source->addQAPair("qa_001", "What is Boost?", "Boost is a C++ libraries collection.", "libraries");

    auto found = source->findQAPair("qa_001");
    ASSERT_TRUE(found.has_value(), "QA pair found");
    ASSERT_STR_EQ("What is Boost?", found->question, "Question matches");
    ASSERT_STR_EQ("Boost is a C++ libraries collection.", found->answer, "Answer matches");
    ASSERT_STR_EQ("libraries", found->category, "Category matches");

    auto not_found = source->findQAPair("qa_999");
    ASSERT_TRUE(!not_found.has_value(), "Non-existent QA pair returns nullopt");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_update_pair) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_update.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_update";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    source->addQAPair("qa_001", "Original question?", "Original answer.", "cat1");

    bool updated = source->updateQAPair("qa_001", "Updated answer.", "cat2");
    ASSERT_TRUE(updated, "QA pair updated");

    auto found = source->findQAPair("qa_001");
    ASSERT_TRUE(found.has_value(), "Updated pair found");
    ASSERT_STR_EQ("Updated answer.", found->answer, "Answer updated");
    ASSERT_STR_EQ("cat2", found->category, "Category updated");

    bool question_updated = source->updateQAPair("qa_001", "", "", "", "Updated question?");
    ASSERT_TRUE(question_updated, "QA question updated");

    found = source->findQAPair("qa_001");
    ASSERT_TRUE(found.has_value(), "Question-updated pair found");
    ASSERT_STR_EQ("Updated question?", found->question, "Question updated");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_delete_pair) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_delete.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_delete";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    source->addQAPair("qa_001", "Question?", "Answer.", "cat");
    ASSERT_EQ(1, source->count(), "Count is 1");

    bool deleted = source->deleteQAPair("qa_001");
    ASSERT_TRUE(deleted, "QA pair deleted");
    ASSERT_EQ(0, source->count(), "Count is 0 after delete");

    auto found = source->findQAPair("qa_001");
    ASSERT_TRUE(!found.has_value(), "Deleted pair not found");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_search_by_category) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_category.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_cat";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    source->addQAPair("qa_001", "Q1?", "A1.", "setup");
    source->addQAPair("qa_002", "Q2?", "A2.", "setup");
    source->addQAPair("qa_003", "Q3?", "A3.", "runtime");

    auto setup_pairs = source->searchByCategory("setup");
    ASSERT_EQ(2, setup_pairs.size(), "Found 2 setup pairs");

    auto runtime_pairs = source->searchByCategory("runtime");
    ASSERT_EQ(1, runtime_pairs.size(), "Found 1 runtime pair");

    auto empty_pairs = source->searchByCategory("nonexistent");
    ASSERT_EQ(0, empty_pairs.size(), "Found 0 nonexistent pairs");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_pagination) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_paginate.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_pag";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    // Add 25 pairs
    for (int i = 0; i < 25; ++i) {
        source->addQAPair(
            "qa_" + std::to_string(i),
            "Question " + std::to_string(i) + "?",
            "Answer " + std::to_string(i) + ".",
            "general"
        );
    }
    ASSERT_EQ(25, source->count(), "Total 25 pairs");

    auto page1 = source->getAllPairs(1, 10);
    ASSERT_EQ(10, page1.size(), "Page 1 has 10 items");

    auto page3 = source->getAllPairs(3, 10);
    ASSERT_EQ(5, page3.size(), "Page 3 has 5 items (last page)");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_migrate) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_migrate.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_mig";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    bool migrated = source->migrate();
    ASSERT_TRUE(migrated, "Migration successful");

    // Verify tables exist
    ASSERT_TRUE(std::filesystem::exists(db_path), "Database file created");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_hash_uniqueness) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_hash.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_hash";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    // Add same QA pair twice — second should fail due to hash uniqueness
    bool first = source->addQAPair("qa_001", "What is RAG?", "RAG is Retrieval-Augmented Generation.", "ai");
    ASSERT_TRUE(first, "First add successful");

    bool second = source->addQAPair("qa_002", "What is RAG?", "RAG is Retrieval-Augmented Generation.", "ai");
    ASSERT_FALSE(second, "Duplicate add rejected (hash-based dedup)");

    ASSERT_EQ(1, source->count(), "Only 1 pair in database");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_persistence) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_persist.db";
    cleanup_test_db(db_path);

    // Create and populate
    {
        SQLiteSource::Config config;
        config.db_path = db_path;
        config.source_id = "test_persist";
        config.auto_migrate = true;

        auto source = std::make_shared<SQLiteSource>(config);
        source->initialize();
        source->addQAPair("qa_001", "Persisted question?", "Persisted answer.", "test");
        source->cleanup();
    }

    // Reopen and verify
    {
        SQLiteSource::Config config;
        config.db_path = db_path;
        config.source_id = "test_persist";
        config.auto_migrate = true;

        auto source = std::make_shared<SQLiteSource>(config);
        source->initialize();
        ASSERT_EQ(1, source->count(), "Count preserved after restart");

        auto found = source->findQAPair("qa_001");
        ASSERT_TRUE(found.has_value(), "Pair found after restart");
        ASSERT_STR_EQ("Persisted question?", found->question, "Question preserved");

        source->cleanup();
    }

    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_get_documents) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_docs.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_docs";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    source->addQAPair("qa_001", "Doc question?", "Doc answer.", "docs");

    auto docs = source->getDocuments();
    ASSERT_EQ(1, docs.size(), "Got 1 document");
    ASSERT_TRUE(!docs[0].content.empty(), "Document content not empty");
    ASSERT_TRUE(docs[0].metadata.count("qa_id"), "Document has qa_id metadata");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

// ============================================
// Main
// ============================================

int main() {
    std::cout << "=== SQLiteSource Tests ===" << std::endl;

#if QORNIX_HAS_SQLITE
    RUN_TEST(sqlite_source_basic);
    RUN_TEST(sqlite_source_add_pair);
    RUN_TEST(sqlite_source_find_pair);
    RUN_TEST(sqlite_source_update_pair);
    RUN_TEST(sqlite_source_delete_pair);
    RUN_TEST(sqlite_source_search_by_category);
    RUN_TEST(sqlite_source_pagination);
    RUN_TEST(sqlite_source_migrate);
    RUN_TEST(sqlite_source_hash_uniqueness);
    RUN_TEST(sqlite_source_persistence);
    RUN_TEST(sqlite_source_get_documents);
#else
    std::cout << "SKIPPED: SQLite3 not available" << std::endl;
#endif

    std::cout << "\n=== Results: " << test_count << " tests, " << fail_count << " failures ===" << std::endl;
    return fail_count > 0 ? 1 : 0;
}
