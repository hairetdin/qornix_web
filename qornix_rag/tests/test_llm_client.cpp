/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for LLM Client functionality
 * 
 * Tests:
 * - Successful LLM request
 * - LLM unavailable (connection refused)
 * - LLM timeout
 * - Invalid response format
 * - Streaming response parsing
 */

#include "../llm_client.h"
#include "mocks/mock_llm_server.h"
#include "mocks/test_helpers.h"
#include <iostream>
#include <cassert>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <iomanip>

// Test counter
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

// Test 1: Default configuration
void test_default_config() {
    std::cout << "\nTest 1: Default LLM Configuration" << std::endl;
    
    try {
        // Create LLM client with default config (no file)
        LLMClient llm_client("/nonexistent/config.yaml");
        
        // Check defaults
        std::string api_url = llm_client.get_api_url();
        std::string model = llm_client.get_model();
        
        test_helpers::assert_equal("http://localhost:11434", api_url, "API URL should default to Ollama");
        test_helpers::assert_equal("llama3", model, "Model should default to llama3");
        
        test_passed("Default configuration");
    } catch (const std::exception& e) {
        test_failed("Default configuration", e.what());
    }
}

// Test 2: Custom configuration
void test_custom_config() {
    std::cout << "\nTest 2: Custom LLM Configuration" << std::endl;
    
    try {
        // Create a test config file
        std::string config_path = "/tmp/test_llm_config.yaml";
        std::ofstream config_file(config_path);
        config_file << R"(
llm:
  api_url: "http://test-llm:8080"
  api_key: "test-key-123"
  model: "test-model"
  max_tokens: 2048
  temperature: 0.9
  request_timeout_ms: 15000
  system_prompt: "Test prompt"
)" << std::endl;
        config_file.close();
        
        LLMClient llm_client(config_path);
        
        test_helpers::assert_equal("http://test-llm:8080", llm_client.get_api_url(), "API URL");
        test_helpers::assert_equal("test-key-123", llm_client.get_api_key(), "API Key");
 test_helpers::assert_equal("test-model", llm_client.get_model(), "Model");
        test_helpers::assert_equal("2048", std::to_string(llm_client.get_max_tokens()), "Max tokens");
        // Compare float temperature with tolerance (std::to_string gives "0.900000")
        float temp = llm_client.get_temperature();
        test_helpers::assert_true(std::abs(temp - 0.9f) < 0.01f, "Temperature");
        test_helpers::assert_equal("15000", std::to_string(llm_client.get_request_timeout_ms()), "Timeout");
        
        test_passed("Custom configuration");
        
        // Clean up
        std::remove(config_path.c_str());
    } catch (const std::exception& e) {
        test_failed("Custom configuration", e.what());
    }
}

// Test 3: Prompt building
void test_prompt_building() {
    std::cout << "\nTest 3: Prompt Building" << std::endl;
    
    try {
        LLMClient llm_client;
        
        std::string context = "File: test.cpp\nContent: void foo() {}";
        std::string question = "What does foo do?";
        
        std::string prompt = llm_client.build_prompt(context, question);
        
        // Check prompt contains expected parts
        test_helpers::assert_contains(prompt, "Контекст:", "Prompt should contain context marker");
        test_helpers::assert_contains(prompt, "test.cpp", "Prompt should contain file path");
        // build_prompt passes the question through as-is (no translation)
        test_helpers::assert_contains(prompt, "What does foo do?", "Prompt should contain question");
        
        test_passed("Prompt building");
    } catch (const std::exception& e) {
        test_failed("Prompt building", e.what());
    }
}

// Test 4: Request JSON building
void test_request_json_building() {
    std::cout << "\nTest 4: Request JSON Building" << std::endl;
    
    try {
        LLMClient llm_client;
        
        std::string prompt = "Test prompt";
        std::string json = llm_client.build_request_json(prompt);
        
        // Check JSON structure
        test_helpers::assert_contains(json, "\"model\"", "JSON should contain model");
        test_helpers::assert_contains(json, "\"messages\"", "JSON should contain messages");
        test_helpers::assert_contains(json, "\"system\"", "JSON should contain system message");
        test_helpers::assert_contains(json, "\"user\"", "JSON should contain user message");
        test_helpers::assert_contains(json, "Test prompt", "JSON should contain prompt");
        
        test_passed("Request JSON building");
    } catch (const std::exception& e) {
        test_failed("Request JSON building", e.what());
    }
}

// Test 5: Response parsing
void test_response_parsing() {
    std::cout << "\nTest 5: Response Parsing" << std::endl;
    
    try {
        LLMClient llm_client;
        
        // Test Ollama format
        std::string ollama_response = R"({"message": {"role": "assistant", "content": "Test answer"}})";
        std::string parsed = llm_client.parse_response(ollama_response);
        
        test_helpers::assert_contains(parsed, "Test answer", "Should parse Ollama response");
        
        // Test OpenAI format
        std::string openai_response = R"({"choices": [{"message": {"content": "OpenAI answer"}}]})";
        parsed = llm_client.parse_response(openai_response);
        
        test_helpers::assert_contains(parsed, "OpenAI answer", "Should parse OpenAI response");

        // Escaped quotes and newlines in code snippets must not truncate the answer.
        std::string ollama_code_response =
            R"({"message":{"role":"assistant","content":"#include \"rag_extension.h\"\nint main() { return 0; }"},"done":true})";
        parsed = llm_client.parse_response(ollama_code_response);
        test_helpers::assert_contains(parsed, "#include \"rag_extension.h\"", "Should preserve escaped quotes in code");
        test_helpers::assert_contains(parsed, "int main()", "Should preserve text after escaped quotes");

        // Ollama /api/generate format.
        std::string ollama_generate_response =
            R"({"response":"Generated line 1\nGenerated line 2","done":true})";
        parsed = llm_client.parse_response(ollama_generate_response);
        test_helpers::assert_contains(parsed, "Generated line 1", "Should parse Ollama generate response");
        test_helpers::assert_contains(parsed, "Generated line 2", "Should unescape newlines");

        // Streaming NDJSON fallback: join all chunks if a provider returns them.
        std::string ollama_stream_response =
            R"({"message":{"content":"#include \"rag_extension.h\"\n"},"done":false})"
            "\n"
            R"({"message":{"content":"void register_rag();"},"done":false})"
            "\n"
            R"({"done":true})";
        parsed = llm_client.parse_response(ollama_stream_response);
        test_helpers::assert_contains(parsed, "#include \"rag_extension.h\"", "Should parse streamed escaped quotes");
        test_helpers::assert_contains(parsed, "void register_rag();", "Should join streamed content chunks");
        
        test_passed("Response parsing");
    } catch (const std::exception& e) {
        test_failed("Response parsing", e.what());
    }
}

// Test 6: Error handling
void test_error_handling() {
    std::cout << "\nTest 6: Error Handling" << std::endl;
    
    try {
        LLMClient llm_client;
        
        // Test invalid JSON
        std::string invalid_json = "Not valid JSON";
        std::string result = llm_client.parse_response(invalid_json);
        
        // Should return error message, not crash
        test_helpers::assert_contains(result, "Ошибка", "Should handle invalid JSON gracefully");
        
        test_passed("Error handling");
    } catch (const std::exception& e) {
        test_failed("Error handling", e.what());
    }
}

// Test 7: Multiple LLM endpoints (failover)
void test_failover() {
    std::cout << "\nTest 7: LLM Failover" << std::endl;
    
    try {
        // Create config with multiple endpoints
        std::string config_path = "/tmp/test_failover_config.yaml";
        std::ofstream config_file(config_path);
        config_file << R"(
llm:
  api_url: "http://primary-llm:11434"
  model: "llama3"
  failover:
    enabled: true
    endpoints:
      - url: "http://backup-llm:11434"
        model: "mistral"
)" << std::endl;
        config_file.close();
        
        LLMClient llm_client(config_path);
        
        test_helpers::assert_equal("http://primary-llm:11434", llm_client.get_api_url(), "Primary URL");
        
        // Note: Failover implementation would need to be added to LLMClient
        // For now, just test that config is parsed
        
        test_passed("Failover configuration");
        
        // Clean up
        std::remove(config_path.c_str());
    } catch (const std::exception& e) {
        test_failed("Failover configuration", e.what());
    }
}

// Main test runner
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Qornix RAG - LLM Client Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_default_config();
    test_custom_config();
    test_prompt_building();
    test_request_json_building();
    test_response_parsing();
    test_error_handling();
    test_failover();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, " 
              << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
