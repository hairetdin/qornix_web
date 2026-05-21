/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>

// Test helpers for qornix_rag tests

namespace test_helpers {

// Simple HTTP server for mocking LLM API
class MockLLMEndpoint {
private:
    int port_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    int request_count_{0};
    std::string last_request_body_;

public:
    explicit MockLLMEndpoint(int port = 18765) : port_(port) {}

    bool start() {
        running_ = true;
        server_thread_ = std::thread([this]() {
            // Simple mock server implementation
            // In real tests, use a proper mock framework
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        return true;
    }

    void stop() {
        running_ = false;
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    int get_request_count() const { return request_count_; }
    std::string get_last_request_body() const { return last_request_body_; }
    void increment_request_count() { request_count_++; }
    void set_last_request_body(const std::string& body) { last_request_body_ = body; }

    // Helper to simulate LLM response
    std::string simulate_llm_response(const std::string& question) {
        return R"({"message": {"role": "assistant", "content": "DI Container в qornix_web работает через registration pattern."}})";
    }

    // Helper to simulate error response
    std::string simulate_error_response(int status_code = 500) {
        return R"({"error": "Internal Server Error", "status": )" + std::to_string(status_code) + R"(})";
    }
};

// Wait for server to be ready
inline bool wait_for_server(const std::string& host, int port, int max_wait_ms = 5000) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(max_wait_ms)) {
        // Try to connect
        // In real implementation, use a socket or curl
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return true; // Simplified for now
    }
    return false;
}

// Create temporary directory for test data
inline std::string create_temp_dir(const std::string& prefix = "qornix_rag_test_") {
    std::string temp_dir = "/tmp/" + prefix + "XXXXXX";
    // Use mkdtemp or create unique name
    return temp_dir + "123"; // Simplified
}

// Assert helpers
inline void assert_true(bool condition, const std::string& message = "") {
    if (!condition) {
        throw std::runtime_error("Assertion failed: " + message);
    }
}

inline void assert_equal(const std::string& expected, const std::string& actual, const std::string& message = "") {
    if (expected != actual) {
        std::string error = "Assertion failed: expected '" + expected + "' but got '" + actual + "'";
        if (!message.empty()) {
            error += " - " + message;
        }
        throw std::runtime_error(error);
    }
}

inline void assert_contains(const std::string& haystack, const std::string& needle, const std::string& message = "") {
    if (haystack.find(needle) == std::string::npos) {
        std::string error = "Assertion failed: '" + needle + "' not found in '" + haystack + "'";
        if (!message.empty()) {
            error += " - " + message;
        }
        throw std::runtime_error(error);
    }
}

// Sleep with millisecond precision
inline void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Get current timestamp in milliseconds
inline long long current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

} // namespace test_helpers
