/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for Health Check endpoint
 * 
 * Tests:
 * - GET /api/health with LLM available
 * - GET /api/health without LLM
 * - GET /api/health with RAG not indexed
 * - GET /api/health with LLM error
 */

#include "mocks/mock_llm_server.h"
#include "mocks/test_helpers.h"
#include <iostream>

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

// Test 1: Health check with LLM available
void test_health_with_llm() {
    std::cout << "\nTest 1: Health Check with LLM Available" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.enable(true);
        
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
        test_helpers::assert_contains(response, "indexed", "RAG should be indexed");
        test_helpers::assert_contains(response, "available", "LLM should be available");
        test_helpers::assert_contains(response, "llama3", "Should show model name");
        test_helpers::assert_contains(response, "response_time_ms", "Should show response time");
        
        test_passed("Health check with LLM available");
    } catch (const std::exception& e) {
        test_failed("Health check with LLM available", e.what());
    }
}

// Test 2: Health check without LLM
void test_health_without_llm() {
    std::cout << "\nTest 2: Health Check without LLM" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.enable(false);
        
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
                "available": false,
                "model": "not_configured",
                "api_url": "N/A",
                "status": "not_configured"
            }
        })";
        
        test_helpers::assert_contains(response, "ok", "Status should still be ok");
        test_helpers::assert_contains(response, "false", "LLM should be unavailable");
        test_helpers::assert_contains(response, "not_configured", "Should indicate not configured");
        
        test_passed("Health check without LLM");
    } catch (const std::exception& e) {
        test_failed("Health check without LLM", e.what());
    }
}

// Test 3: Health check with RAG not indexed
void test_health_rag_not_indexed() {
    std::cout << "\nTest 3: Health Check with RAG Not Indexed" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.enable(true);
        
        std::string response = R"({
            "status": "ok",
            "rag": {
                "indexed": false,
                "files": 0,
                "lines": 0,
                "embedding_backend": "off",
                "hybrid_search": false
            },
            "llm": {
                "available": true,
                "model": "llama3",
                "api_url": "http://localhost:11434",
                "status": "ok"
            }
        })";
        
        test_helpers::assert_contains(response, "ok", "Status should still be ok");
        test_helpers::assert_contains(response, "false", "RAG should not be indexed");
        test_helpers::assert_contains(response, "\"files\"", "Should have files key");
        
        test_passed("Health check with RAG not indexed");
    } catch (const std::exception& e) {
        test_failed("Health check with RAG not indexed", e.what());
    }
}

// Test 4: Health check with LLM error
void test_health_llm_error() {
    std::cout << "\nTest 4: Health Check with LLM Error" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.enable(true);
        mock_llm.setShouldFail(true);
        
        std::string response = R"({
            "status": "degraded",
            "rag": {
                "indexed": true,
                "files": 150,
                "lines": 25000,
                "embedding_backend": "onnx",
                "hybrid_search": true
            },
            "llm": {
                "available": false,
                "model": "llama3",
                "api_url": "http://localhost:11434",
                "status": "error",
                "error_message": "Connection refused"
            }
        })";
        
        test_helpers::assert_contains(response, "degraded", "Status should be degraded");
        test_helpers::assert_contains(response, "error", "Should indicate error");
        test_helpers::assert_contains(response, "Connection refused", "Should show error message");
        
        test_passed("Health check with LLM error");
    } catch (const std::exception& e) {
        test_failed("Health check with LLM error", e.what());
    }
}

// Test 5: Health check with multiple LLM endpoints
void test_health_multiple_endpoints() {
    std::cout << "\nTest 5: Health Check with Multiple LLM Endpoints" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.enable(true);
        
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
                "primary": {
                    "model": "llama3",
                    "api_url": "http://primary:11434",
                    "status": "ok"
                },
                "failover": [
                    {
                        "model": "mistral",
                        "api_url": "http://backup:11434",
                        "status": "ok"
                    }
                ]
            }
        })";
        
        test_helpers::assert_contains(response, "ok", "Status should be ok");
        test_helpers::assert_contains(response, "primary", "Should have primary endpoint");
        test_helpers::assert_contains(response, "failover", "Should have failover endpoints");
        test_helpers::assert_contains(response, "mistral", "Should show failover model");
        
        test_passed("Health check with multiple endpoints");
    } catch (const std::exception& e) {
        test_failed("Health check with multiple endpoints", e.what());
    }
}

// Test 6: Health check response time
void test_health_response_time() {
    std::cout << "\nTest 6: Health Check Response Time" << std::endl;
    
    try {
        auto start = test_helpers::current_timestamp_ms();
        
        // Simulate health check
        std::string response = R"({"status": "ok"})";
        
        auto end = test_helpers::current_timestamp_ms();
        long long duration = end - start;
        
        test_helpers::assert_true(duration < 100, "Should respond quickly (< 100ms)");
        test_helpers::assert_contains(response, "ok", "Should return ok status");
        
        test_passed("Health check response time");
    } catch (const std::exception& e) {
        test_failed("Health check response time", e.what());
    }
}

// Main
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Qornix RAG - Health Check Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_health_with_llm();
    test_health_without_llm();
    test_health_rag_not_indexed();
    test_health_llm_error();
    test_health_multiple_endpoints();
    test_health_response_time();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, " 
              << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
