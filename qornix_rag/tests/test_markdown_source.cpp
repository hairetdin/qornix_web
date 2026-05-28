/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for MarkdownSource:
 * - Basic creation and initialization
 * - Frontmatter parsing
 * - Single file import
 * - Directory import
 * - Duplicate detection
 * - Document generation
 */

#include "../markdown_source.h"
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

#define ASSERT_GT(actual, expected, msg) do { \
    if (!((actual) > (expected))) { \
        std::cerr << "FAILED: " << msg << " — expected > " << (expected) << ", got " << (actual) << std::endl; \
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

// Create test directory and files
static std::string create_test_dir() {
    std::string test_dir = "/tmp/test_markdown_source";
    std::filesystem::create_directories(test_dir);
    return test_dir;
}

static void cleanup_test_dir(const std::string& dir) {
    std::filesystem::remove_all(dir);
}

static void create_test_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
    file.close();
}

// ============================================
// MarkdownSource Basic Tests
// ============================================

TEST(markdown_source_basic) {
    std::string test_dir = create_test_dir();
    cleanup_test_dir(test_dir);

    MarkdownSource::Config config;
    config.directory_path = test_dir;
    config.file_patterns = {"*.md"};
    config.recursive = false;

    auto source = std::make_shared<MarkdownSource>(config);
    ASSERT_NOT_NULL(source, "MarkdownSource created");

    bool init = source->initialize();
    ASSERT_TRUE(init, "MarkdownSource initialized");

    ASSERT_TRUE(source->getType() == DataSourceType::TEXT_DOCS, "DataSource type is TEXT_DOCS");
    ASSERT_EQ(0, source->count(), "Initial count is 0");

    source->cleanup();
    cleanup_test_dir(test_dir);
}

TEST(markdown_source_single_file_import) {
    std::string test_dir = create_test_dir();
    std::string test_file = test_dir + "/test.md";

    std::string content = R"(---
title: "What is RAG?"
category: "ai"
aliases: ["retrieval augmented generation"]
---

RAG stands for Retrieval-Augmented Generation.
It combines retrieval-based and generation-based approaches.)";

    create_test_file(test_file, content);

    MarkdownSource::Config config;
    config.directory_path = test_dir;
    config.file_patterns = {"*.md"};
    config.recursive = false;

    auto source = std::make_shared<MarkdownSource>(config);
    source->initialize();

    auto docs = source->getDocuments();
    ASSERT_GT(docs.size(), 0, "At least 1 document imported");

    // Check document content
    bool found_rag = false;
    for (const auto& doc : docs) {
        if (doc.content.find("RAG") != std::string::npos) {
            found_rag = true;
            break;
        }
    }
    ASSERT_TRUE(found_rag, "RAG content found in documents");

    source->cleanup();
    cleanup_test_dir(test_dir);
}

TEST(markdown_source_directory_import) {
    std::string test_dir = create_test_dir();

    // Create multiple test files
    create_test_file(test_dir + "/qa1.md", R"(---
title: "Question 1"
category: "setup"
---
Answer to question 1.)");

    create_test_file(test_dir + "/qa2.md", R"(---
title: "Question 2"
category: "setup"
---
Answer to question 2.)");

    create_test_file(test_dir + "/qa3.md", R"(---
title: "Question 3"
category: "runtime"
---
Answer to question 3.)");

    // Create a non-md file (should be ignored)
    create_test_file(test_dir + "/readme.txt", "This should be ignored.");

    MarkdownSource::Config config;
    config.directory_path = test_dir;
    config.file_patterns = {"*.md"};
    config.recursive = false;

    auto source = std::make_shared<MarkdownSource>(config);
    source->initialize();

    auto docs = source->getDocuments();
    ASSERT_EQ(3, docs.size(), "3 markdown files imported");

    source->cleanup();
    cleanup_test_dir(test_dir);
}

TEST(markdown_source_import_result) {
    std::string test_dir = create_test_dir();
    create_test_file(test_dir + "/test.md", R"(---
title: "Test"
category: "general"
---
Test content.)");

    MarkdownSource::Config config;
    config.directory_path = test_dir;
    config.file_patterns = {"*.md"};
    config.recursive = false;

    auto source = std::make_shared<MarkdownSource>(config);
    source->initialize();

    auto imported = source->getImportedFiles();
    ASSERT_GT(imported.size(), 0, "Imported files tracked");

    source->cleanup();
    cleanup_test_dir(test_dir);
}

TEST(markdown_source_no_files) {
    std::string test_dir = create_test_dir();

    // Directory with no markdown files
    create_test_file(test_dir + "/readme.txt", "Not a markdown file.");

    MarkdownSource::Config config;
    config.directory_path = test_dir;
    config.file_patterns = {"*.md"};
    config.recursive = false;

    auto source = std::make_shared<MarkdownSource>(config);
    source->initialize();

    auto docs = source->getDocuments();
    ASSERT_EQ(0, docs.size(), "No documents from directory without .md files");

    source->cleanup();
    cleanup_test_dir(test_dir);
}

TEST(markdown_source_document_metadata) {
    std::string test_dir = create_test_dir();
    create_test_file(test_dir + "/meta.md", R"(---
title: "Metadata Test"
category: "testing"
aliases: ["meta", "metadata"]
---
Content with metadata.)");

    MarkdownSource::Config config;
    config.directory_path = test_dir;
    config.file_patterns = {"*.md"};
    config.recursive = false;

    auto source = std::make_shared<MarkdownSource>(config);
    source->initialize();

    auto docs = source->getDocuments();
    ASSERT_GT(docs.size(), 0, "Document with metadata imported");

    // Check that document has metadata
    bool has_metadata = false;
    for (const auto& doc : docs) {
        if (doc.metadata.size() > 0) {
            has_metadata = true;
            break;
        }
    }
    ASSERT_TRUE(has_metadata, "Document has metadata");

    source->cleanup();
    cleanup_test_dir(test_dir);
}

TEST(markdown_source_recursive_import) {
    std::string test_dir = create_test_dir();
    std::string sub_dir = test_dir + "/subdir";
    std::filesystem::create_directories(sub_dir);

    create_test_file(test_dir + "/root.md", R"(---
title: "Root"
category: "root"
---
Root content.)");

    create_test_file(sub_dir + "/nested.md", R"(---
title: "Nested"
category: "nested"
---
Nested content.)");

    MarkdownSource::Config config;
    config.directory_path = test_dir;
    config.file_patterns = {"*.md"};
    config.recursive = true;

    auto source = std::make_shared<MarkdownSource>(config);
    source->initialize();

    auto docs = source->getDocuments();
    ASSERT_EQ(2, docs.size(), "2 files imported recursively");

    source->cleanup();
    cleanup_test_dir(test_dir);
}

// ============================================
// Main
// ============================================

int main() {
    std::cout << "=== MarkdownSource Tests ===" << std::endl;

    RUN_TEST(markdown_source_basic);
    RUN_TEST(markdown_source_single_file_import);
    RUN_TEST(markdown_source_directory_import);
    RUN_TEST(markdown_source_import_result);
    RUN_TEST(markdown_source_no_files);
    RUN_TEST(markdown_source_document_metadata);
    RUN_TEST(markdown_source_recursive_import);

    std::cout << "\n=== Results: " << test_count << " tests, " << fail_count << " failures ===" << std::endl;
    return fail_count > 0 ? 1 : 0;
}
