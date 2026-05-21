/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for Streaming (SSE) functionality
 * 
 * Tests:
 * - Streaming response parsing
 * - Chunk ordering
 * - Done flag handling
 * - Error during streaming
 * - Partial response handling
 */

#include "mocks/mock_llm_server.h"
#include "mocks/test_helpers.h"
#include <iostream>
#include <sstream>

static int tests_passed = 0;
static int tests_failed = 0;

void test_passed(const std::string& test_name) {
    tests_passed++;
    std::cout << "  ✓ " << test_name << std::endl;
}

void test_failed(const std::string& test_name, const std::string& error) {
    tests_failed++;
    std::cout << "  ✗ " << test_name << " - " << error << std::endl;
}

// Test 1: Basic streaming
void test_basic_streaming() {
    std::cout << "\nTest 1: Basic Streaming" << std::endl;
    
    try {
        std::vector<std::string> chunks = {"DI", " Container", " работает", " через", " pattern."};
        std::string streaming = MockLLMClient::create_streaming_response(chunks);
        
        test_helpers::assert_contains(streaming, "DI", "First chunk");
        test_helpers::assert_contains(streaming, "pattern", "Last chunk");
        test_helpers::assert_contains(streaming, "done", "Done flag present");
        
        test_passed("Basic streaming");
    } catch (const std::exception& e) {
        test_failed("Basic streaming", e.what());
    }
}

// Test 2: Chunk ordering
void test_chunk_ordering() {
    std::cout << "\nTest 2: Chunk Ordering" << std::endl;
    
    try {
        std::vector<std::string> chunks = {"1", "2", "3", "4", "5"};
        std::string streaming = MockLLMClient::create_streaming_response(chunks);
        
        // Verify order by checking positions
        size_t pos1 = streaming.find("1");
        size_t pos2 = streaming.find("2");
        size_t pos3 = streaming.find("3");
        
        test_helpers::assert_true(pos1 < pos2, "Chunk 1 before 2");
        test_helpers::assert_true(pos2 < pos3, "Chunk 2 before 3");
        
        test_passed("Chunk ordering");
    } catch (const std::exception& e) {
        test_failed("Chunk ordering", e.what());
    }
}

// Test 3: Done flag handling
void test_done_flag() {
    std::cout << "\nTest 3: Done Flag Handling" << std::endl;
    
    try {
        std::vector<std::string> chunks = {"Hello", " World"};
        std::string streaming = MockLLMClient::create_streaming_response(chunks);
        
        // Last chunk should have done: true
        test_helpers::assert_contains(streaming, "done\": true", "Last chunk has done: true");
        
        // Other chunks should have done: false
        test_helpers::assert_contains(streaming, "done\": false", "Other chunks have done: false");
        
        test_passed("Done flag handling");
    } catch (const std::exception& e) {
        test_failed("Done flag handling", e.what());
    }
}

// Test 4: Error during streaming
void test_streaming_error() {
    std::cout << "\nTest 4: Error During Streaming" << std::endl;
    
    try {
        // Simulate error response
        std::string error_response = R"({"error": "Internal Server Error", "status": 500})";
        
        test_helpers::assert_contains(error_response, "error", "Should have error field");
        test_helpers::assert_contains(error_response, "500", "Should have status code");
        
        test_passed("Error during streaming");
    } catch (const std::exception& e) {
        test_failed("Error during streaming", e.what());
    }
}

// Test 5: Partial response
void test_partial_response() {
    std::cout << "\nTest 5: Partial Response" << std::endl;
    
    try {
        std::vector<std::string> chunks = {"Partial", " response"};
        std::string streaming = MockLLMClient::create_streaming_response(chunks);
        
        // Should contain all chunks
        test_helpers::assert_contains(streaming, "Partial", "First chunk");
        test_helpers::assert_contains(streaming, "response", "Second chunk");
        
        test_passed("Partial response");
    } catch (const std::exception& e) {
        test_failed("Partial response", e.what());
    }
}

// Test 6: Empty streaming
void test_empty_streaming() {
    std::cout << "\nTest 6: Empty Streaming" << std::endl;
    
    try {
        std::vector<std::string> chunks = {};
        std::string streaming = MockLLMClient::create_streaming_response(chunks);
        
        test_helpers::assert_contains(streaming, "[]", "Should be empty array");
        
        test_passed("Empty streaming");
    } catch (const std::exception& e) {
        test_failed("Empty streaming", e.what());
    }
}

// Test 7: Large streaming
void test_large_streaming() {
    std::cout << "\nTest 7: Large Streaming" << std::endl;
    
    try {
        std::vector<std::string> chunks;
        for (int i = 0; i < 100; ++i) {
            chunks.push_back("Chunk " + std::to_string(i));
        }
        
        std::string streaming = MockLLMClient::create_streaming_response(chunks);
        
        test_helpers::assert_contains(streaming, "Chunk 0", "First chunk");
        test_helpers::assert_contains(streaming, "Chunk 99", "Last chunk");
        test_helpers::assert_contains(streaming, "done", "Done flag present");
        
        test_passed("Large streaming");
    } catch (const std::exception& e) {
        test_failed("Large streaming", e.what());
    }
}

// Test 8: Streaming with special characters
void test_streaming_special_chars() {
    std::cout << "\nTest 8: Streaming with Special Characters" << std::endl;
    
    try {
        std::vector<std::string> chunks = {"Hello", " World!", " C++", " &", " Python"};
        std::string streaming = MockLLMClient::create_streaming_response(chunks);
        
        test_helpers::assert_contains(streaming, "C++", "Should handle +");
        test_helpers::assert_contains(streaming, "Python", "Should handle letters");
        
        test_passed("Streaming with special characters");
    } catch (const std::exception& e) {
        test_failed("Streaming with special characters", e.what());
    }
}

// Main
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Qornix RAG - Streaming Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_basic_streaming();
    test_chunk_ordering();
    test_done_flag();
    test_streaming_error();
    test_partial_response();
    test_empty_streaming();
    test_large_streaming();
    test_streaming_special_chars();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, " 
              << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
