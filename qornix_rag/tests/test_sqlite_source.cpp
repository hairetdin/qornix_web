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
#include "../document_chunker.h"
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

TEST(sqlite_source_server_side_qa_list_and_suggest) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_qa_list.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_qa_list";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    source->addQAPair("qa_001", "How to configure Ollama?", "Set llm.provider to ollama.", "llm");
    source->addQAPair("qa_002",
                      "How to rebuild the vector index?",
                      "Run ingestion or indexing again.",
                      "operations",
                      "[\"reindex\"]",
                      "{\"tags\":\"[\\\"vectors\\\",\\\"ops\\\"]\",\"source\":\"runbook\"}");
    source->addQAPair("qa_003", "How to change the RAG port?", "Pass --port to the launcher.", "operations");
    source->addQAPair("qa_004", "Where are QA pairs stored?", "SQLite stores QA pairs locally.", "storage");

    SQLiteSource::QAListOptions options;
    options.query = "How";
    options.category = "operations";
    options.limit = 1;
    options.offset = 0;
    auto page1 = source->listQAPairs(options);
    ASSERT_EQ(2, page1.total, "Filtered total counts all matching operations rows");
    ASSERT_EQ(1, page1.items.size(), "Filtered page size is enforced");
    ASSERT_EQ(1, page1.limit, "Limit is echoed");
    ASSERT_EQ(0, page1.offset, "Offset is echoed");

    options.offset = 1;
    auto page2 = source->listQAPairs(options);
    ASSERT_EQ(2, page2.total, "Filtered total stable on page 2");
    ASSERT_EQ(1, page2.items.size(), "Second page has one row");
    ASSERT_TRUE(page2.items[0].id != page1.items[0].id, "Second page returns a different row");

    SQLiteSource::QAListOptions tag_options;
    tag_options.tag = "vectors";
    tag_options.limit = 10;
    auto tag_page = source->listQAPairs(tag_options);
    ASSERT_EQ(1, tag_page.total, "Tag filter finds vector QA row");
    ASSERT_EQ(1, tag_page.items.size(), "Tag page returns vector QA row");
    ASSERT_STR_EQ("qa_002", tag_page.items[0].id, "Tag filter id matches");
    ASSERT_EQ(2, tag_page.items[0].tags.size(), "Tags parsed from metadata");
    ASSERT_EQ(1, tag_page.items[0].aliases.size(), "Aliases parsed from JSON");
    ASSERT_STR_EQ("runbook", tag_page.items[0].metadata["source"], "Metadata parsed from JSON");

    auto suggestions = source->suggestQAPairs("vector", 5);
    ASSERT_EQ(1, suggestions.size(), "Suggestion finds vector question");
    ASSERT_STR_EQ("qa_002", suggestions[0].id, "Suggestion id matches");

    auto categories = source->listQACategories("", 10);
    ASSERT_TRUE(std::find(categories.begin(), categories.end(), "llm") != categories.end(), "Category list includes llm");
    ASSERT_TRUE(std::find(categories.begin(), categories.end(), "operations") != categories.end(), "Category list includes operations");
    ASSERT_TRUE(std::find(categories.begin(), categories.end(), "storage") != categories.end(), "Category list includes storage");

    auto filtered_categories = source->listQACategories("oper", 10);
    ASSERT_EQ(1, filtered_categories.size(), "Category suggestion filters by query");
    ASSERT_STR_EQ("operations", filtered_categories[0], "Filtered category matches");

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

TEST(sqlite_source_persist_index_snapshot) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_index_snapshot.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_index";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    Document doc;
    doc.path = "/tmp/project/doc/guide.md";
    doc.relative_path = "doc/guide.md";
    doc.content = "# Guide\nPersistent RAG storage keeps document chunks and embeddings.";
    doc.type = "config";
    doc.language = "text";
    doc.size_bytes = doc.content.size();
    doc.lines_count = 2;
    doc.hash = HashCalculator::compute_md5(doc.content);
    doc.last_modified = std::chrono::system_clock::now();
    doc.embedding = {0.1f, 0.2f, 0.3f};
    doc.metadata["source"] = "unit_test";

    Document stale = doc;
    stale.path = "/tmp/project/doc/stale.md";
    stale.relative_path = "doc/stale.md";
    stale.content = "# Stale\nThis document should be removed on the next snapshot.";
    stale.hash = HashCalculator::compute_md5(stale.content);

    auto persisted = source->persistIndexedDocuments({doc, stale}, "project:test", "tfidf:3", "tfidf");
    ASSERT_EQ(2, persisted.documents, "Persisted two documents");
    ASSERT_EQ(2, persisted.chunks, "Persisted two chunks");
    ASSERT_EQ(2, persisted.embeddings, "Persisted two embeddings");
    ASSERT_EQ(0, persisted.deleted_documents, "No stale delete on first snapshot");
    ASSERT_EQ(2, source->countPersistedDocuments("project:test"), "Document table count");
    ASSERT_EQ(2, source->countPersistedChunks("project:test"), "Chunk table count");
    ASSERT_EQ(2, source->countPersistedEmbeddings("project:test"), "Embedding table count");
    auto embeddings = source->listPersistedEmbeddings("project:test", "tfidf:3");
    ASSERT_EQ(2, embeddings.size(), "Persisted embeddings can be exported");
    ASSERT_STR_EQ("project:test", embeddings.front().source_id, "Exported embedding source id");
    ASSERT_STR_EQ("tfidf:3", embeddings.front().model_id, "Exported embedding model id");
    ASSERT_EQ(3, embeddings.front().vector.embedding.size(), "Exported embedding dimension");
    ASSERT_TRUE(embeddings.front().vector.label < embeddings.back().vector.label, "Export labels are stable and increasing");

    auto found = source->findPersistedDocument("doc/guide.md", "project:test");
    ASSERT_TRUE(found.has_value(), "Persisted document found");
    ASSERT_STR_EQ("doc/guide.md", found->relative_path, "Persisted relative path");
    ASSERT_STR_EQ("unit_test", found->metadata["source"], "Persisted metadata");

    doc.content = "# Guide\nUpdated content replaces the previous chunk.";
    doc.hash = HashCalculator::compute_md5(doc.content);
    doc.embedding = {0.4f, 0.5f, 0.6f};

    persisted = source->persistIndexedDocuments({doc}, "project:test", "tfidf:3", "tfidf");
    ASSERT_EQ(1, persisted.documents, "Re-persisted one document");
    ASSERT_EQ(1, persisted.deleted_documents, "Removed one stale document");
    ASSERT_EQ(1, source->countPersistedDocuments("project:test"), "Document count replaced, not duplicated");
    ASSERT_EQ(1, source->countPersistedChunks("project:test"), "Chunk count replaced, not duplicated");
    ASSERT_EQ(1, source->countPersistedEmbeddings("project:test"), "Embedding count replaced, not duplicated");
    auto stale_found = source->findPersistedDocument("doc/stale.md", "project:test");
    ASSERT_TRUE(!stale_found.has_value(), "Stale document removed");
    ASSERT_TRUE(source->deletePersistedDocument("doc/guide.md", "project:test"), "Persisted document delete succeeds");
    ASSERT_EQ(0, source->countPersistedDocuments("project:test"), "Document delete cascades from persisted index");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_ingestion_jobs) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_ingestion_jobs.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_jobs";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    const std::string job_id = "job_001";
    ASSERT_TRUE(source->recordIngestionJobStarted(job_id, "project:/tmp/project", "/tmp/project"),
                "Job start recorded");

    qornix::rag::IngestionJobResult result;
    result.files_seen = 3;
    result.documents_imported = 2;
    result.duplicates_found = 1;
    result.skipped = 0;
    result.errors = 0;
    ASSERT_TRUE(source->recordIngestionJobFinished(job_id, "completed", result),
                "Job finish recorded");

    auto found = source->findIngestionJob(job_id);
    ASSERT_TRUE(found.has_value(), "Job found");
    ASSERT_STR_EQ("completed", found->status, "Job status persisted");
    ASSERT_EQ(3, found->files_seen, "Job files_seen persisted");
    ASSERT_EQ(2, found->documents_imported, "Job documents_imported persisted");

    auto jobs = source->listIngestionJobs();
    ASSERT_EQ(1, jobs.size(), "Job history list contains record");
    ASSERT_STR_EQ(job_id, jobs.front().id, "Job history returns job id");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}

TEST(sqlite_source_persist_chunked_document_snapshot) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_chunked_snapshot.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_chunked_index";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    source->initialize();

    Document doc;
    doc.path = "/tmp/project/doc/large.md";
    doc.relative_path = "doc/large.md";
    doc.type = "markdown";
    doc.language = "text";
    doc.content = "# Large\n";
    for (size_t i = 0; i < 75; ++i) {
        doc.content += "chunkword" + std::to_string(i) + " ";
    }
    doc.size_bytes = doc.content.size();
    doc.lines_count = 2;
    doc.hash = HashCalculator::compute_md5(doc.content);
    doc.last_modified = std::chrono::system_clock::now();

    DocumentChunker chunker({20, 5, 3});
    auto chunks = chunker.chunkDocument(doc);
    ASSERT_TRUE(chunks.size() > 1, "Document chunker creates multiple chunks");
    for (auto& chunk : chunks) {
        chunk.embedding = {0.1f, 0.2f, 0.3f};
    }

    auto persisted = source->persistIndexedDocuments(chunks, "project:chunked", "tfidf:3", "tfidf");
    ASSERT_EQ(1, persisted.documents, "Chunked snapshot keeps one source document");
    ASSERT_EQ(chunks.size(), persisted.chunks, "Chunked snapshot persists all chunks");
    ASSERT_EQ(chunks.size(), persisted.embeddings, "Chunked snapshot persists one embedding per chunk");
    ASSERT_EQ(1, source->countPersistedDocuments("project:chunked"), "One persisted source document");
    ASSERT_EQ(chunks.size(), source->countPersistedChunks("project:chunked"), "All chunks persisted");
    ASSERT_EQ(chunks.size(), source->countPersistedEmbeddings("project:chunked"), "All chunk embeddings persisted");

    auto found = source->findPersistedDocument("doc/large.md", "project:chunked");
    ASSERT_TRUE(found.has_value(), "Source document lookup works after chunked persistence");

    source->cleanup();
    cleanup_test_db(db_path);
#else
    std::cout << "SKIPPED (SQLite not available)";
#endif
}


TEST(sqlite_source_qa_auxiliary_quality) {
#if QORNIX_HAS_SQLITE
    std::string db_path = "/tmp/test_sqlite_source_qa_auxiliary_quality.db";
    cleanup_test_db(db_path);

    SQLiteSource::Config config;
    config.db_path = db_path;
    config.source_id = "test_qa_quality";
    config.auto_migrate = true;

    auto source = std::make_shared<SQLiteSource>(config);
    ASSERT_TRUE(source->initialize(), "SQLiteSource initialized");

    ASSERT_TRUE(source->addQAPair("qa_001",
                                  "How do I configure Ollama?",
                                  "Use the ollama provider and model settings.",
                                  "llm",
                                  "[]",
                                  "{\"tags\": \"[\\\"ollama\\\",\\\"local-llm\\\"]\", \"source\": \"runbook\"}"),
                "QA with tags added");

    auto tags = source->listQATags("olla", 10);
    ASSERT_TRUE(std::find(tags.begin(), tags.end(), "ollama") != tags.end(), "Normalized QA tags are searchable");

    SQLiteSource::QAListOptions options;
    options.query = "provider model";
    options.limit = 10;
    auto listed = source->listQAPairs(options);
    ASSERT_EQ(1, listed.total, "QA list search finds answer text through FTS/LIKE fallback");
    ASSERT_STR_EQ("qa_001", listed.items.front().id, "QA list search returns expected pair");

    ASSERT_TRUE(source->updateQAPair("qa_001",
                                     "Use the Ollama provider and set the model in config.yaml.",
                                     "llm",
                                     "[]",
                                     "How do I configure local Ollama?",
                                     "{\"tags\": \"[\\\"ollama\\\",\\\"config\\\"]\"}"),
                "QA update succeeds");
    auto updated_tags = source->listQATags("config", 10);
    ASSERT_TRUE(std::find(updated_tags.begin(), updated_tags.end(), "config") != updated_tags.end(), "Updated tags are indexed");

    auto history = source->getQAPairHistory("qa_001", 10);
    ASSERT_TRUE(history.size() >= 2, "QA version history records create and update");
    ASSERT_STR_EQ("update", history.front().action, "Latest history action is update");
    ASSERT_TRUE(history.front().version >= 2, "History version increments");

    ASSERT_TRUE(source->deleteQAPair("qa_001"), "QA delete succeeds");
    auto delete_history = source->getQAPairHistory("qa_001", 10);
    ASSERT_TRUE(!delete_history.empty(), "Delete history is retained");
    ASSERT_STR_EQ("delete", delete_history.front().action, "Latest history action is delete");

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
    RUN_TEST(sqlite_source_server_side_qa_list_and_suggest);
    RUN_TEST(sqlite_source_qa_auxiliary_quality);
    RUN_TEST(sqlite_source_migrate);
    RUN_TEST(sqlite_source_hash_uniqueness);
    RUN_TEST(sqlite_source_persistence);
    RUN_TEST(sqlite_source_get_documents);
    RUN_TEST(sqlite_source_persist_index_snapshot);
    RUN_TEST(sqlite_source_ingestion_jobs);
    RUN_TEST(sqlite_source_persist_chunked_document_snapshot);
#else
    std::cout << "SKIPPED: SQLite3 not available" << std::endl;
#endif

    std::cout << "\n=== Results: " << test_count << " tests, " << fail_count << " failures ===" << std::endl;
    return fail_count > 0 ? 1 : 0;
}
