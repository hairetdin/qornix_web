/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

/**
 * Mock LLM Server for testing
 * 
 * Simulates an OpenAI-compatible LLM API endpoint
 * Used for testing LLM client without external dependencies
 */

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <iostream>

class MockLLMClient {
public:
    // Callback types
    using ResponseCallback = std::function<std::string(const std::string& request)>;
    using ErrorCallback = std::function<std::string(int status_code)>;

private:
    bool enabled_;
    ResponseCallback success_callback_;
    ErrorCallback error_callback_;
    int response_delay_ms_;
    bool should_fail_;
    int fail_after_requests_;
    int request_count_;

public:
    MockLLMClient()
        : enabled_(true),
          response_delay_ms_(0),
          should_fail_(false),
          fail_after_requests_(0),
          request_count_(0) {
        
        // Default success callback
        success_callback_ = [this](const std::string& request) {
            return R"({
                "message": {
                    "role": "assistant",
                    "content": "DI Container в qornix_web работает через registration pattern."
                }
            })";
        };
        
        error_callback_ = [this](int status_code) {
            return R"({"error": "Internal Server Error", "status": )" + std::to_string(status_code) + R"(})";
        };
    }

    // Configure behavior
    void enable(bool enabled) { enabled_ = enabled; }
    void setResponseDelay(int ms) { response_delay_ms_ = ms; }
    void setShouldFail(bool fail) { should_fail_ = fail; }
    void setFailAfterRequests(int count) { fail_after_requests_ = count; }
    
    void setSuccessCallback(ResponseCallback cb) { success_callback_ = cb; }
    void setErrorCallback(ErrorCallback cb) { error_callback_ = cb; }

    // Simulate LLM response
    std::string simulate_request(const std::string& request_body) {
        request_count_++;
        
        // Simulate delay
        if (response_delay_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(response_delay_ms_));
        }
        
        // Check if should fail (fail the first N requests where N = fail_after_requests_)
        // N=0 means "fail immediately" (all requests)
        if (should_fail_ && (fail_after_requests_ == 0 || request_count_ <= fail_after_requests_)) {
            return error_callback_(500);
        }
        
        return success_callback_(request_body);
    }

    // Get statistics
    int getRequestCount() const { return request_count_; }
    bool isEnabled() const { return enabled_; }

    // Static helpers for common scenarios
    static std::string create_success_response(const std::string& answer) {
        return R"({"message": {"role": "assistant", "content": ")" + answer + R"("}})";
    }

    static std::string create_streaming_response(const std::vector<std::string>& chunks) {
        std::string result = "[";
        for (size_t i = 0; i < chunks.size(); ++i) {
            result += R"({"chunk": ")" + chunks[i] + R"(", "done": )" + 
                     (i == chunks.size() - 1 ? "true" : "false") + "}";
            if (i < chunks.size() - 1) {
                result += ", ";
            }
        }
        result += "]";
        return result;
    }

    static std::string create_error_response(int status_code, const std::string& message) {
        return R"({"error": ")" + message + R"(", "status": )" + std::to_string(status_code) + R"(})";
    }
};

// Test fixture base class
class LLMTestFixture {
protected:
    MockLLMClient mock_llm_;
    std::string test_project_path_;
    
    LLMTestFixture() : test_project_path_("/tmp/qornix_rag_test_project") {}
    
    virtual ~LLMTestFixture() = default;
    
    void setup() {
        mock_llm_.enable(true);
        mock_llm_.setResponseDelay(0);
        mock_llm_.setShouldFail(false);
    }
    
    void teardown() {
        mock_llm_.enable(false);
    }
};
