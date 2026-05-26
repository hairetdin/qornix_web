/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for DataSource implementations:
 * - MemorySource basic operations
 * - QASource QA pair management
 * - DataSource type conversion
 * - RagEngine multi-source integration
 */

#include "../memory_source.h"
#include "../qa_source.h"
#include "../data_source.h"
#include "../core.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <sstream>

using qornix::rag::MemorySource;
using qornix::rag::QASource;

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

static int test_count = 0;
static int fail_count = 0;

// ============================================
// MemorySource Tests
// ============================================

TEST(memory_source_basic) {
    MemorySource::Config config;
    config.name = "Test Memory Source";
    config.allow_duplicates = false;

    auto source = std::make_shared<MemorySource>(config);
    ASSERT_NOT_NULL(source, "MemorySource created");
    ASSERT_EQ(0, static_cast<int>(source->count()), "Initial count is 0");
    ASSERT_TRUE(source->getType() == DataSourceType::MEMORY, "Type is MEMORY");
    ASSERT_TRUE(source->initialize(), "initialize() returns true");
}

TEST(memory_source_add_document) {
    MemorySource::Config config;
    config.name = "Test Memory Source";

    auto source = std::make_shared<MemorySource>(config);

    Document doc;
    doc.path = "test.txt";
    doc.relative_path = "test.txt";
    doc.content = "Test document content";
    doc.type = "text";
    doc.language = "text";
    doc.size_bytes = doc.content.size();

    source->addDocument(doc);
    ASSERT_EQ(1, static_cast<int>(source->count()), "Count is 1 after add");

    source->addDocument(doc); // Duplicate
    ASSERT_EQ(1, static_cast<int>(source->count()), "Count stays 1 (no duplicates)");
}

TEST(memory_source_remove_document) {
    MemorySource::Config config;
    config.name = "Test Memory Source";

    auto source = std::make_shared<MemorySource>(config);

    Document doc;
    doc.path = "test.txt";
    doc.relative_path = "test.txt";
    doc.content = "Test content";
    doc.type = "text";
    doc.language = "text";
    doc.size_bytes = doc.content.size();

    source->addDocument(doc);
    ASSERT_EQ(1, static_cast<int>(source->count()), "Count is 1");

    source->removeDocument("test.txt");
    ASSERT_EQ(0, static_cast<int>(source->count()), "Count is 0 after remove");
}

TEST(memory_source_get_documents) {
    MemorySource::Config config;
    config.name = "Test Memory Source";

    auto source = std::make_shared<MemorySource>(config);

    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.path = "file_" + std::to_string(i) + ".txt";
        doc.relative_path = doc.path;
        doc.content = "Content " + std::to_string(i);
        doc.type = "text";
        doc.language = "text";
        doc.size_bytes = doc.content.size();
        source->addDocument(doc);
    }

    auto docs = source->getDocuments();
    ASSERT_EQ(5, static_cast<int>(docs.size()), "Got 5 documents");
}

TEST(memory_source_clear) {
    MemorySource::Config config;
    config.name = "Test Memory Source";

    auto source = std::make_shared<MemorySource>(config);

    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.path = "file_" + std::to_string(i) + ".txt";
        doc.relative_path = doc.path;
        doc.content = "Content";
        doc.type = "text";
        doc.language = "text";
        doc.size_bytes = doc.content.size();
        source->addDocument(doc);
    }

    ASSERT_EQ(10, static_cast<int>(source->count()), "Count is 10");

    source->clear();
    ASSERT_EQ(0, static_cast<int>(source->count()), "Count is 0 after clear");
}

// ============================================
// QASource Tests
// ============================================

TEST(qa_source_basic) {
    QASource::Config config;
    config.name = "Test QA KB";
    config.source_id = "test_qa";

    auto source = std::make_shared<QASource>(config);
    ASSERT_NOT_NULL(source, "QASource created");
    ASSERT_EQ(0, static_cast<int>(source->count()), "Initial count is 0");
    ASSERT_TRUE(source->getType() == DataSourceType::QA_KB, "Type is QA_KB");
    ASSERT_TRUE(source->initialize(), "initialize() returns true");
}

TEST(qa_source_add_pair) {
    QASource::Config config;
    config.name = "Test QA KB";
    config.source_id = "test_qa";

    auto source = std::make_shared<QASource>(config);

    QASource::QAPair pair;
    pair.id = "qa_001";
    pair.question = "What is C++?";
    pair.answer = "C++ is a programming language.";
    pair.category = "programming";
    pair.aliases = {"cpp", "cplusplus"};

    source->addQAPair(pair);
    ASSERT_EQ(1, static_cast<int>(source->count()), "Count is 1 after add");
}

TEST(qa_source_find_pair) {
    QASource::Config config;
    config.name = "Test QA KB";

    auto source = std::make_shared<QASource>(config);

    QASource::QAPair pair;
    pair.id = "qa_001";
    pair.question = "What is DI?";
    pair.answer = "DI is Dependency Injection.";
    pair.category = "architecture";

    source->addQAPair(pair);

    auto found = source->findQAPair("What is DI?");
    ASSERT_TRUE(found.has_value(), "Found QA pair by question");
    ASSERT_EQ("What is DI?", found.value().question, "Question matches");
    ASSERT_EQ("DI is Dependency Injection.", found.value().answer, "Answer matches");
}

TEST(qa_source_remove_pair) {
    QASource::Config config;
    config.name = "Test QA KB";

    auto source = std::make_shared<QASource>(config);

    QASource::QAPair pair;
    pair.id = "qa_001";
    pair.question = "Question?";
    pair.answer = "Answer.";
    pair.category = "general";

    source->addQAPair(pair);
    ASSERT_EQ(1, static_cast<int>(source->count()), "Count is 1");

    source->removeQAPair("qa_001");
    ASSERT_EQ(0, static_cast<int>(source->count()), "Count is 0 after remove");
}

TEST(qa_source_search_by_category) {
    QASource::Config config;
    config.name = "Test QA KB";

    auto source = std::make_shared<QASource>(config);

    QASource::QAPair pair1;
    pair1.id = "qa_001";
    pair1.question = "What is DI?";
    pair1.answer = "Dependency Injection.";
    pair1.category = "architecture";

    QASource::QAPair pair2;
    pair2.id = "qa_002";
    pair2.question = "How to build?";
    pair2.answer = "Run cmake.";
    pair2.category = "setup";

    source->addQAPair(pair1);
    source->addQAPair(pair2);

    auto arch_pairs = source->searchByCategory("architecture");
    ASSERT_EQ(1, static_cast<int>(arch_pairs.size()), "Found 1 architecture pair");

    auto setup_pairs = source->searchByCategory("setup");
    ASSERT_EQ(1, static_cast<int>(setup_pairs.size()), "Found 1 setup pair");
}

TEST(qa_source_get_all_pairs) {
    QASource::Config config;
    config.name = "Test QA KB";

    auto source = std::make_shared<QASource>(config);

    for (int i = 0; i < 5; i++) {
        QASource::QAPair pair;
        pair.id = "qa_" + std::to_string(i);
        pair.question = "Question " + std::to_string(i);
        pair.answer = "Answer " + std::to_string(i);
        pair.category = "general";
        source->addQAPair(pair);
    }

    auto all = source->getAllPairs();
    ASSERT_EQ(5, static_cast<int>(all.size()), "Got 5 pairs");
}

TEST(qa_source_update_pair) {
    QASource::Config config;
    config.name = "Test QA KB";

    auto source = std::make_shared<QASource>(config);

    QASource::QAPair pair;
    pair.id = "qa_001";
    pair.question = "Original question?";
    pair.answer = "Original answer.";
    pair.category = "general";

    source->addQAPair(pair);

    // Update
    pair.answer = "Updated answer.";
    source->updateQAPair(pair);

    auto found = source->findQAPair("Original question?");
    ASSERT_TRUE(found.has_value(), "Pair still exists after update");
    ASSERT_EQ("Updated answer.", found.value().answer, "Answer was updated");
}

TEST(qa_source_to_document) {
    QASource::Config config;
    config.name = "Test QA KB";

    auto source = std::make_shared<QASource>(config);

    QASource::QAPair pair;
    pair.id = "qa_001";
    pair.question = "What is RAG?";
    pair.answer = "RAG is Retrieval-Augmented Generation.";
    pair.category = "ai";

    source->addQAPair(pair);

    auto docs = source->getDocuments();
    ASSERT_EQ(1, static_cast<int>(docs.size()), "Got 1 document");
    ASSERT_TRUE(docs[0].content.find("What is RAG?") != std::string::npos, "Document contains question");
    ASSERT_TRUE(docs[0].content.find("RAG is Retrieval-Augmented Generation.") != std::string::npos, "Document contains answer");
}

TEST(qa_source_json_import_export) {
    QASource::Config config;
    config.name = "Test QA KB";
    auto source = std::make_shared<QASource>(config);

    const std::string input = R"({"pairs":[{"id":"qa_001","question":"How to tag QA?","answer":"Use tags.","category":"docs","aliases":["tag question"],"tags":["qa","docs"],"metadata":{"source":"unit"}}]})";
    ASSERT_TRUE(source->loadFromJson(input), "QA JSON import succeeds");
    auto found = source->findQAPair("How to tag QA?");
    ASSERT_TRUE(found.has_value(), "Imported QA pair found");
    ASSERT_EQ(2, static_cast<int>(found->tags.size()), "Imported tags parsed");
    ASSERT_EQ(1, static_cast<int>(found->aliases.size()), "Imported aliases parsed");
    ASSERT_EQ("unit", found->metadata["source"], "Imported metadata parsed");

    auto exported = source->toJson();
    ASSERT_TRUE(exported.find("\"tags\"") != std::string::npos, "Export contains tags field");
    ASSERT_TRUE(exported.find("tag question") != std::string::npos, "Export contains aliases");
}

// ============================================
// DataSource Type Tests
// ============================================

TEST(data_source_type_to_string) {
    ASSERT_EQ("FILESYSTEM", data_source_type_to_string(DataSourceType::FILESYSTEM), "FILESYSTEM string");
    ASSERT_EQ("QA_KB", data_source_type_to_string(DataSourceType::QA_KB), "QA_KB string");
    ASSERT_EQ("TEXT_DOCS", data_source_type_to_string(DataSourceType::TEXT_DOCS), "TEXT_DOCS string");
    ASSERT_EQ("DATABASE", data_source_type_to_string(DataSourceType::DATABASE), "DATABASE string");
    ASSERT_EQ("MEMORY", data_source_type_to_string(DataSourceType::MEMORY), "MEMORY string");
    ASSERT_EQ("CUSTOM", data_source_type_to_string(DataSourceType::CUSTOM), "CUSTOM string");
}

// ============================================
// RagEngine Multi-Source Tests
// ============================================

TEST(rag_engine_add_data_source) {
    RagEngineConfig config;
    RagEngine engine(config);

    MemorySource::Config mem_config;
    mem_config.name = "Test Memory Source";
    mem_config.source_id = "test_memory_source";
    auto mem_source = std::make_shared<MemorySource>(mem_config);

    engine.addDataSource(mem_source);

    auto sources = engine.getDataSources();
    ASSERT_EQ(1, static_cast<int>(sources.size()), "Engine has 1 data source");
}

TEST(rag_engine_remove_data_source) {
    RagEngineConfig config;
    RagEngine engine(config);

    MemorySource::Config mem_config;
    mem_config.name = "Test Memory Source";
    mem_config.source_id = "test_memory_source";
    auto mem_source = std::make_shared<MemorySource>(mem_config);

    engine.addDataSource(mem_source);
    ASSERT_EQ(1, static_cast<int>(engine.getDataSources().size()), "Engine has 1 source");

    engine.removeDataSource("test_memory_source");
    ASSERT_EQ(0, static_cast<int>(engine.getDataSources().size()), "Engine has 0 sources after remove");
}

TEST(rag_engine_multi_source) {
    RagEngineConfig config;
    RagEngine engine(config);

    // Add MemorySource
    MemorySource::Config mem_config;
    mem_config.name = "Memory Source";
    auto mem_source = std::make_shared<MemorySource>(mem_config);

    Document doc;
    doc.path = "test.txt";
    doc.relative_path = "test.txt";
    doc.content = "Memory source document";
    doc.type = "text";
    doc.language = "text";
    doc.size_bytes = doc.content.size();
    mem_source->addDocument(doc);

    // Add QASource
    QASource::Config qa_config;
    qa_config.name = "QA Source";
    qa_config.source_id = "qa_test";
    auto qa_source = std::make_shared<QASource>(qa_config);

    QASource::QAPair pair;
    pair.id = "qa_001";
    pair.question = "What is this?";
    pair.answer = "This is a test.";
    pair.category = "test";
    qa_source->addQAPair(pair);

    engine.addDataSource(mem_source);
    engine.addDataSource(qa_source);

    auto sources = engine.getDataSources();
    ASSERT_EQ(2, static_cast<int>(sources.size()), "Engine has 2 data sources");

    // Check types
    bool has_memory = false;
    bool has_qa = false;
    for (const auto& src : sources) {
        if (src->getType() == DataSourceType::MEMORY) has_memory = true;
        if (src->getType() == DataSourceType::QA_KB) has_qa = true;
    }
    ASSERT_TRUE(has_memory, "Has MEMORY source");
    ASSERT_TRUE(has_qa, "Has QA_KB source");
}

// ============================================
// Main
// ============================================

int main() {
    std::cout << "=== DataSource Tests ===" << std::endl;
    std::cout << std::endl;

    // MemorySource tests
    RUN_TEST(memory_source_basic);
    RUN_TEST(memory_source_add_document);
    RUN_TEST(memory_source_remove_document);
    RUN_TEST(memory_source_get_documents);
    RUN_TEST(memory_source_clear);

    // QASource tests
    RUN_TEST(qa_source_basic);
    RUN_TEST(qa_source_add_pair);
    RUN_TEST(qa_source_find_pair);
    RUN_TEST(qa_source_remove_pair);
    RUN_TEST(qa_source_search_by_category);
    RUN_TEST(qa_source_get_all_pairs);
    RUN_TEST(qa_source_update_pair);
    RUN_TEST(qa_source_to_document);
    RUN_TEST(qa_source_json_import_export);

    // DataSource type tests
    RUN_TEST(data_source_type_to_string);

    // RagEngine multi-source tests
    RUN_TEST(rag_engine_add_data_source);
    RUN_TEST(rag_engine_remove_data_source);
    RUN_TEST(rag_engine_multi_source);

    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Tests: " << test_count << std::endl;
    std::cout << "Failed: " << fail_count << std::endl;

    if (fail_count == 0) {
        std::cout << "ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
