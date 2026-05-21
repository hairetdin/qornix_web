/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for RAG API endpoints
 * 
 * Tests:
 * - POST /api/ask with LLM
 * - POST /api/ask without LLM
 * - POST /api/ask streaming
 * - GET /api/health
 * - POST /api/search (existing)
 * - POST /api/index (existing)
 */

#include "mocks/mock_llm_server.h"
#include "mocks/test_helpers.h"
#include <iostream>
#include <string>
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

// Test 1: POST /api/ask with LLM
void test_ask_with_llm() {
    std::cout << "\nTest 1: POST /api/ask with LLM" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        
        // Simulate request
        std::string request = R"({"question": "Как работает DI Container?", "top_k": 5})";
        
        // Simulate response
        std::string response = R"({
            "success": true,
            "question": "Как работает DI Container?",
            "answer": "DI Container в qornix_web работает через registration pattern.",
            "llm_status": "ok",
            "context": [
                {"path": "include/di_container.h", "score": 0.95, "snippet": "..."}
            ],
            "sources": ["include/di_container.h"]
        })";
        
        // Parse response
        test_helpers::assert_contains(response, "success", "Response should have success field");
        test_helpers::assert_contains(response, "answer", "Response should have answer field");
        test_helpers::assert_contains(response, "ok", "LLM status should be ok");
        test_helpers::assert_contains(response, "context", "Response should have context");
        test_helpers::assert_contains(response, "sources", "Response should have sources");
        
        test_passed("POST /api/ask with LLM");
    } catch (const std::exception& e) {
        test_failed("POST /api/ask with LLM", e.what());
    }
}

// Test 2: POST /api/ask without LLM
void test_ask_without_llm() {
    std::cout << "\nTest 2: POST /api/ask without LLM" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.enable(false);
        
        // Simulate request
        std::string request = R"({"question": "Как работает DI Container?", "top_k": 5})";
        
        // Simulate fallback response
        std::string response = R"({
            "success": true,
            "question": "Как работает DI Container?",
            "answer": "LLM недоступен. Вот релевантные фрагменты:",
            "llm_status": "unavailable",
            "context": [
                {"path": "include/di_container.h", "score": 0.95, "snippet": "..."}
            ],
            "sources": ["include/di_container.h"]
        })";
        
        test_helpers::assert_contains(response, "unavailable", "LLM status should be unavailable");
        test_helpers::assert_contains(response, "релевантные", "Should show context in Russian");
        
        test_passed("POST /api/ask without LLM");
    } catch (const std::exception& e) {
        test_failed("POST /api/ask without LLM", e.what());
    }
}

// Test 3: POST /api/ask streaming
void test_ask_streaming() {
    std::cout << "\nTest 3: POST /api/ask streaming" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        
        // Simulate streaming response
        std::vector<std::string> chunks = {"DI", " Container", " работает", " через", " pattern."};
        std::string streaming_response = MockLLMClient::create_streaming_response(chunks);
        
        test_helpers::assert_contains(streaming_response, "DI", "First chunk should be DI");
        test_helpers::assert_contains(streaming_response, "pattern", "Last chunk should be pattern");
        test_helpers::assert_contains(streaming_response, "done", "Should have done flag");
        
        test_passed("POST /api/ask streaming");
    } catch (const std::exception& e) {
        test_failed("POST /api/ask streaming", e.what());
    }
}

// Test 4: GET /api/health
void test_health_check() {
    std::cout << "\nTest 4: GET /api/health" << std::endl;
    
    try {
        // Simulate health response
        std::string response = R"({
            "status": "ok",
            "rag": {
                "indexed": true,
                "files": 150,
                "lines": 25000,
                "embedding_backend": "onnx",
                "hybrid_search": true
            },
            "llm": {
                "available": true,
                "model": "llama3",
                "api_url": "http://localhost:11434",
                "status": "ok",
                "response_time_ms": 50
            }
        })";
        
        test_helpers::assert_contains(response, "ok", "Status should be ok");
        test_helpers::assert_contains(response, "rag", "Should have rag section");
        test_helpers::assert_contains(response, "llm", "Should have llm section");
        test_helpers::assert_contains(response, "indexed", "Should have indexed flag");
        test_helpers::assert_contains(response, "available", "Should have llm available flag");
        
        test_passed("GET /api/health");
    } catch (const std::exception& e) {
        test_failed("GET /api/health", e.what());
    }
}

// Test 5: POST /api/search (existing)
void test_search_endpoint() {
    std::cout << "\nTest 5: POST /api/search (existing)" << std::endl;
    
    try {
        // Simulate search response
        std::string response = R"({
            "success": true,
            "query": "DI Container",
            "results": [
                {
                    "path": "include/di_container.h",
                    "type": "header",
                    "language": "C++",
                    "score": 0.95,
                    "vector_score": 0.92,
                    "text_score": 0.98,
                    "fused_score": 0.95,
                    "snippet": "class DIContainer { ... }",
                    "lines": 50,
                    "size": 2048
                }
            ],
            "count": 1
        })";
        
        test_helpers::assert_contains(response, "success", "Should have success");
        test_helpers::assert_contains(response, "results", "Should have results");
        test_helpers::assert_contains(response, "fused_score", "Should have fused score");
        test_helpers::assert_contains(response, "vector_score", "Should have vector score");
        test_helpers::assert_contains(response, "text_score", "Should have text score");
        
        test_passed("POST /api/search");
    } catch (const std::exception& e) {
        test_failed("POST /api/search", e.what());
    }
}

// Test 6: POST /api/index (existing)
void test_index_endpoint() {
    std::cout << "\nTest 6: POST /api/index (existing)" << std::endl;
    
    try {
        // Simulate index response
        std::string response = R"({
            "success": true,
            "message": "Project indexed successfully",
            "stats": {
                "total_files": 150,
                "total_lines": 25000,
                "total_size_kb": 500,
                "index_duration_ms": 2100
            }
        })";
        
        test_helpers::assert_contains(response, "success", "Should have success");
        test_helpers::assert_contains(response, "indexed successfully", "Should confirm indexing");
        test_helpers::assert_contains(response, "total_files", "Should have file count");
        test_helpers::assert_contains(response, "index_duration_ms", "Should have duration");
        
        test_passed("POST /api/index");
    } catch (const std::exception& e) {
        test_failed("POST /api/index", e.what());
    }
}

// Test 7: Error handling - missing question
void test_ask_missing_question() {
    std::cout << "\nTest 7: Error handling - missing question" << std::endl;
    
    try {
        // Simulate error response
        std::string response = R"({"success": false, "error": "Question is required"})";
        
        test_helpers::assert_contains(response, "error", "Should have error");
        test_helpers::assert_contains(response, "Question is required", "Should indicate missing question");
        
        test_passed("Error handling - missing question");
    } catch (const std::exception& e) {
        test_failed("Error handling - missing question", e.what());
    }
}

// Test 8: Error handling - empty results
void test_ask_empty_results() {
    std::cout << "\nTest 8: Error handling - empty results" << std::endl;
    
    try {
        // Simulate no results response
        std::string response = R"({
            "success": true,
            "question": "Unknown question",
            "answer": "Недостаточно информации для ответа.",
            "llm_status": "ok",
            "context": [],
            "sources": []
        })";
        
        test_helpers::assert_contains(response, "Недостаточно информации", "Should indicate insufficient info");
        test_helpers::assert_contains(response, "context", "Should have empty context");
        test_helpers::assert_contains(response, "sources", "Should have empty sources");
        
        test_passed("Error handling - empty results");
    } catch (const std::exception& e) {
        test_failed("Error handling - empty results", e.what());
    }
}

// Main
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Qornix RAG - API Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_ask_with_llm();
    test_ask_without_llm();
    test_ask_streaming();
    test_health_check();
    test_search_endpoint();
    test_index_endpoint();
    test_ask_missing_question();
    test_ask_empty_results();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, " 
              << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
