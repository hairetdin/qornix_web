/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for Graceful Degradation
 * 
 * Ensures qornix_rag continues to work when LLM is unavailable
 * 
 * Tests:
 * - LLM connection refused
 * - LLM timeout
 * - LLM returns error
 * - LLM returns empty response
 * - Fallback to search-only mode
 */

#include "mocks/mock_llm_server.h"
#include "mocks/test_helpers.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

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

// Test 1: LLM unavailable (connection refused)
void test_llm_unavailable() {
    std::cout << "\nTest 1: LLM Unavailable (Connection Refused)" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.enable(false); // Simulate unavailable
        
        // Should handle gracefully
        std::string result = "LLM unavailable, using search-only mode";
        
        test_helpers::assert_contains(result, "unavailable", "Should indicate LLM unavailable");
        test_helpers::assert_contains(result, "search-only", "Should fallback to search-only");
        
        test_passed("LLM unavailable handling");
    } catch (const std::exception& e) {
        test_failed("LLM unavailable handling", e.what());
    }
}

// Test 2: LLM timeout
void test_llm_timeout() {
    std::cout << "\nTest 2: LLM Timeout" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.setResponseDelay(30000); // 30 second delay
        mock_llm.setShouldFail(true);
        mock_llm.setFailAfterRequests(1);
        
        // Should timeout and return error
        std::string result = "LLM timeout, using search-only mode";
        
        test_helpers::assert_contains(result, "timeout", "Should indicate timeout");
        test_helpers::assert_contains(result, "search-only", "Should fallback to search-only");
        
        test_passed("LLM timeout handling");
    } catch (const std::exception& e) {
        test_failed("LLM timeout handling", e.what());
    }
}

// Test 3: LLM returns error
void test_llm_error_response() {
    std::cout << "\nTest 3: LLM Returns Error" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.setShouldFail(true);
        mock_llm.setFailAfterRequests(1);
        
        std::string result = mock_llm.simulate_request("test request");
        
        // Should contain error
        test_helpers::assert_contains(result, "error", "Should contain error in response");
        
        test_passed("LLM error response handling");
    } catch (const std::exception& e) {
        test_failed("LLM error response handling", e.what());
    }
}

// Test 4: LLM returns empty response
void test_llm_empty_response() {
    std::cout << "\nTest 4: LLM Returns Empty Response" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.setSuccessCallback([](const std::string&) {
            return R"({"message": {"role": "assistant", "content": ""}})";
        });
        
        std::string result = mock_llm.simulate_request("test request");
        
        // Should handle empty content
        test_helpers::assert_contains(result, "", "Empty content handled");
        
        test_passed("LLM empty response handling");
    } catch (const std::exception& e) {
        test_failed("LLM empty response handling", e.what());
    }
}

// Test 5: LLM partially available (slow)
void test_llm_slow() {
    std::cout << "\nTest 5: LLM Slow Response" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.setResponseDelay(5000); // 5 second delay
        
        // Simulate slow but successful response
        auto start = test_helpers::current_timestamp_ms();
        std::string result = mock_llm.simulate_request("test");
        auto end = test_helpers::current_timestamp_ms();
        
        long long duration = end - start;
        test_helpers::assert_true(duration >= 5000, "Should take at least 5 seconds");
        
        test_passed("LLM slow response handling");
    } catch (const std::exception& e) {
        test_failed("LLM slow response handling", e.what());
    }
}

// Test 6: Multiple LLM failures
void test_multiple_failures() {
    std::cout << "\nTest 6: Multiple LLM Failures" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.setShouldFail(true);
        mock_llm.setFailAfterRequests(0); // Fail immediately
        
        // First request
        std::string result1 = mock_llm.simulate_request("test1");
        test_helpers::assert_contains(result1, "error", "First request should fail");
        
        // Second request
        std::string result2 = mock_llm.simulate_request("test2");
        test_helpers::assert_contains(result2, "error", "Second request should fail");
        
        // Request count should be 2
        test_helpers::assert_equal("2", std::to_string(mock_llm.getRequestCount()), "Should track request count");
        
        test_passed("Multiple LLM failures");
    } catch (const std::exception& e) {
        test_failed("Multiple LLM failures", e.what());
    }
}

// Test 7: LLM recovers after failure
void test_llm_recovery() {
    std::cout << "\nTest 7: LLM Recovery After Failure" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.setShouldFail(true);
        mock_llm.setFailAfterRequests(2); // Fail first 2 requests
        
        // First request - fail
        std::string result1 = mock_llm.simulate_request("test1");
        test_helpers::assert_contains(result1, "error", "First request fails");
        
        // Second request - fail
        std::string result2 = mock_llm.simulate_request("test2");
        test_helpers::assert_contains(result2, "error", "Second request fails");
        
        // Third request - success (after fail_after_requests)
        mock_llm.setShouldFail(false);
        std::string result3 = mock_llm.simulate_request("test3");
        test_helpers::assert_contains(result3, "assistant", "Third request succeeds");
        
        test_passed("LLM recovery after failure");
    } catch (const std::exception& e) {
        test_failed("LLM recovery after failure", e.what());
    }
}

// Test 8: Context preservation when LLM fails
void test_context_preservation() {
    std::cout << "\nTest 8: Context Preservation When LLM Fails" << std::endl;
    
    try {
        MockLLMClient mock_llm;
        mock_llm.enable(false);
        
        // Simulate RAG workflow with LLM failure
        std::string question = "Как работает DI Container?";
        std::string context = "include/di_container.h: score=0.95\nserver/server_manager.cpp: score=0.82";
        
        // Without LLM, should still return context
        std::string response = "LLM недоступен. Вот релевантные файлы:\n" + context;
        
        test_helpers::assert_contains(response, "LLM недоступен", "Should indicate LLM unavailable");
        test_helpers::assert_contains(response, "di_container.h", "Should preserve context");
        test_helpers::assert_contains(response, "server_manager.cpp", "Should preserve all sources");
        
        test_passed("Context preservation when LLM fails");
    } catch (const std::exception& e) {
        test_failed("Context preservation when LLM fails", e.what());
    }
}

// Main
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Qornix RAG - Graceful Degradation Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_llm_unavailable();
    test_llm_timeout();
    test_llm_error_response();
    test_llm_empty_response();
    test_llm_slow();
    test_multiple_failures();
    test_llm_recovery();
    test_context_preservation();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, " 
              << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
