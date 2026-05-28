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
#include <filesystem>
#include <stdexcept>


std::string read_fixture(const std::string& name) {
    const std::vector<std::filesystem::path> roots = {
        std::filesystem::current_path(),
        std::filesystem::current_path() / "..",
        std::filesystem::current_path() / "../..",
        std::filesystem::current_path() / "../../..",
    };

    for (const auto& root : roots) {
        const auto path = root / "qornix_rag" / "tests" / "fixtures" / "llm_provider" / name;
        std::ifstream file(path);
        if (file.is_open()) {
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    // Direct source-tree execution fallback.
    const auto direct = std::filesystem::path("tests") / "fixtures" / "llm_provider" / name;
    std::ifstream file(direct);
    if (file.is_open()) {
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    throw std::runtime_error("Fixture not found: " + name);
}

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

        // Markdown tables, JSON snippets, escaped backslashes, and multiline code
        // are parsed by Boost.JSON rather than hand-scanned string offsets.
        std::string complex_openai_response =
            R"({"choices":[{"message":{"content":"Таблица:\n| key | value |\n| --- | --- |\n| path | C:\\\\qornix\\\\rag |\n\n```json\n{\"enabled\":true,\"name\":\"qornix\"}\n```\n\n```cpp\nstd::string s = \"quoted\";\n```"},"finish_reason":"stop"}]})";
        auto parsed_result = llm_client.parse_response_result(complex_openai_response);
        test_helpers::assert_equal("ok", parsed_result.status, "Complex JSON response status");
        test_helpers::assert_contains(parsed_result.answer, "| key | value |", "Should preserve markdown table");
        test_helpers::assert_contains(parsed_result.answer, "C:\\\\qornix\\\\rag", "Should preserve escaped backslashes");
        test_helpers::assert_contains(parsed_result.answer, "{\"enabled\":true,\"name\":\"qornix\"}", "Should preserve JSON snippet");
        test_helpers::assert_contains(parsed_result.answer, "std::string s = \"quoted\";", "Should preserve quoted C++ code");

        // SSE data lines are still provider JSON payloads; parse each data line.
        std::string sse_response =
            "data: {\"choices\":[{\"delta\":{\"content\":\"line 1\\n\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"line 2\"},\"finish_reason\":\"stop\"}]}\n\n"
            "data: [DONE]\n";
        parsed_result = llm_client.parse_response_result(sse_response);
        test_helpers::assert_equal("ok", parsed_result.status, "SSE response status");
        test_helpers::assert_contains(parsed_result.answer, "line 1", "Should parse first SSE data chunk");
        test_helpers::assert_contains(parsed_result.answer, "line 2", "Should parse second SSE data chunk");

        // Truncation metadata must be separate from provider availability.
        std::string truncated_response =
            R"({"choices":[{"message":{"content":"partial answer"},"finish_reason":"length"}]})";
        parsed_result = llm_client.parse_response_result(truncated_response);
        test_helpers::assert_equal("truncated", parsed_result.status, "Should mark length finish as truncated");
        test_helpers::assert_true(parsed_result.truncated, "Should expose truncation flag");
        test_helpers::assert_equal("length", parsed_result.finish_reason, "Should expose finish reason");

        // Parser failures are explicit parser errors, not provider-unavailable answers.
        parsed_result = llm_client.parse_response_result(R"({"choices":[{"message":{"content":"unterminated})");
        test_helpers::assert_equal("parser_error", parsed_result.status, "Invalid JSON should be parser_error");
        test_helpers::assert_contains(parsed_result.answer, "Ошибка парсинга LLM", "Parser error should be user-visible");

        parsed_result = llm_client.parse_response_result(R"({"error":{"message":"model not found"}})");
        test_helpers::assert_equal("provider_error", parsed_result.status, "Provider errors should be provider_error");
        test_helpers::assert_contains(parsed_result.answer, "model not found", "Provider error message should be visible");
        
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


// Test 7: Structured provider fixtures
void test_provider_diagnostics_fixtures() {
    std::cout << "\nTest 7: Structured Provider Diagnostics Fixtures" << std::endl;

    try {
        LLMClient llm_client;

        auto ollama_models = llm_client.parse_available_models_response(read_fixture("ollama_tags.json"));
        test_helpers::assert_true(std::find(ollama_models.begin(), ollama_models.end(), "llama3:latest") != ollama_models.end(),
                                  "Should parse Ollama /api/tags model names");
        test_helpers::assert_true(std::find(ollama_models.begin(), ollama_models.end(), "qwen3.6:35b") != ollama_models.end(),
                                  "Should parse second Ollama model name");

        auto openai_models = llm_client.parse_available_models_response(read_fixture("openai_models.json"));
        test_helpers::assert_true(std::find(openai_models.begin(), openai_models.end(), "gpt-4o-mini") != openai_models.end(),
                                  "Should parse OpenAI-compatible /v1/models ids");

        auto vllm_models = llm_client.parse_available_models_response(read_fixture("vllm_models.json"));
        test_helpers::assert_true(std::find(vllm_models.begin(), vllm_models.end(), "Qwen/Qwen2.5-Coder-7B-Instruct") != vllm_models.end(),
                                  "Should parse vLLM model ids");

        auto lm_studio_models = llm_client.parse_available_models_response(read_fixture("lm_studio_models.json"));
        test_helpers::assert_true(std::find(lm_studio_models.begin(), lm_studio_models.end(), "lmstudio-community/Meta-Llama-3.1-8B-Instruct-GGUF") != lm_studio_models.end(),
                                  "Should parse LM Studio model ids");

        auto openai_usage = llm_client.parse_token_stats(read_fixture("openai_usage.json"));
        test_helpers::assert_equal("120", std::to_string(openai_usage.prompt_tokens), "OpenAI prompt tokens");
        test_helpers::assert_equal("34", std::to_string(openai_usage.completion_tokens), "OpenAI completion tokens");
        test_helpers::assert_equal("154", std::to_string(openai_usage.total_tokens), "OpenAI total tokens");

        auto ollama_usage = llm_client.parse_token_stats(read_fixture("ollama_chat_final.json"));
        test_helpers::assert_equal("77", std::to_string(ollama_usage.prompt_tokens), "Ollama prompt eval count");
        test_helpers::assert_equal("23", std::to_string(ollama_usage.completion_tokens), "Ollama eval count");
        test_helpers::assert_equal("100", std::to_string(ollama_usage.total_tokens), "Ollama total tokens synthesized");

        test_passed("Structured provider diagnostics fixtures");
    } catch (const std::exception& e) {
        test_failed("Structured provider diagnostics fixtures", e.what());
    }
}

// Test 8: Unified structured streaming parser
void test_structured_streaming_parser() {
    std::cout << "\nTest 8: Unified Structured Streaming Parser" << std::endl;

    try {
        LLMClient llm_client;

        auto openai_chunks = llm_client.parse_stream_chunks(read_fixture("openai_sse.txt"));
        test_helpers::assert_equal("2", std::to_string(openai_chunks.size()), "OpenAI SSE chunk count");
        test_helpers::assert_equal("Hello", openai_chunks[0], "OpenAI first SSE chunk");
        test_helpers::assert_equal(" world", openai_chunks[1], "OpenAI second SSE chunk");
        auto openai_usage = llm_client.parse_token_stats(read_fixture("openai_sse.txt"));
        test_helpers::assert_equal("7", std::to_string(openai_usage.total_tokens), "OpenAI SSE usage chunk");

        auto ollama_chunks = llm_client.parse_stream_chunks(read_fixture("ollama_ndjson.txt"));
        test_helpers::assert_equal("2", std::to_string(ollama_chunks.size()), "Ollama NDJSON chunk count");
        test_helpers::assert_equal("Привет", ollama_chunks[0], "Ollama first chunk");
        test_helpers::assert_equal(" мир", ollama_chunks[1], "Ollama second chunk");
        auto ollama_usage = llm_client.parse_token_stats(read_fixture("ollama_ndjson.txt"));
        test_helpers::assert_equal("6", std::to_string(ollama_usage.total_tokens), "Ollama NDJSON token total");

        const auto stream_request = llm_client.build_request_json("stream request", true);
        test_helpers::assert_contains(stream_request, "\"stream\":true", "Stream request should be valid JSON with stream flag");
        auto parsed_request = llm_client.parse_response_result(R"({"message":{"content":"ok"}})");
        test_helpers::assert_equal("ok", parsed_request.status, "Sanity parser check");

        test_passed("Unified structured streaming parser");
    } catch (const std::exception& e) {
        test_failed("Unified structured streaming parser", e.what());
    }
}

// Test 9: Multiple LLM endpoints (failover)
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
    test_provider_diagnostics_fixtures();
    test_structured_streaming_parser();
    test_failover();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, " 
              << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
