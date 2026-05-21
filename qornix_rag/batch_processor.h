/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Batch Processor — Phase 3
 *
 * Processes multiple questions in a single batch request.
 * Supports concurrent execution with thread pool and returns
 * results in order with status tracking.
 *
 * Features:
 *   - Batch processing of N questions
 *   - Concurrent execution with configurable thread pool
 *   - Progress tracking (completed/total)
 *   - Per-question status (success/error/cancelled)
 *   - Timeout per question and for entire batch
 */

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <optional>

struct BatchQuestion {
    std::string question;
    std::string context;
    size_t top_k = 5;
    std::string client_ip;
};

struct BatchResult {
    std::string question;
    std::string answer;
    bool success = false;
    std::string error;
    long long response_time_ms = 0;
    size_t tokens_used = 0;
};

enum class BatchStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

struct BatchStats {
    size_t total = 0;
    size_t completed = 0;
    size_t failed = 0;
    size_t cancelled = 0;

    double completion_rate() const {
        return total > 0 ? static_cast<double>(completed + failed + cancelled) / total * 100.0 : 0.0;
    }
};

struct BatchConfig {
    size_t max_concurrent = 4;          // Max concurrent LLM requests
    int question_timeout_ms = 60000;    // Timeout per question
    int batch_timeout_ms = 300000;      // Timeout for entire batch (5 min)
    bool stop_on_error = false;         // Stop batch if one question fails
};

class BatchProcessor {
private:
    BatchConfig config_;
    std::vector<BatchResult> results_;
    std::mutex results_mutex_;
    std::atomic<size_t> completed_count_{0};
    std::atomic<size_t> failed_count_{0};
    std::atomic<BatchStatus> status_{BatchStatus::PENDING};
    std::atomic<bool> cancelled_{false};

    // Progress callback: (completed, total, question_index)
    std::function<void(size_t, size_t, size_t)> progress_callback_;

    // Worker thread function
    void worker_thread(size_t thread_id,
                       const std::vector<BatchQuestion>& questions,
                       size_t start_idx,
                       size_t end_idx,
                       std::shared_ptr<std::function<std::string(const std::string&, const std::string&, const std::string&)>> ask_fn);

public:
    explicit BatchProcessor(const BatchConfig& config = BatchConfig());
    ~BatchProcessor() = default;

    // Non-copyable
    BatchProcessor(const BatchProcessor&) = delete;
    BatchProcessor& operator=(const BatchProcessor&) = delete;

    // Process a batch of questions
    // ask_fn: function that takes (question, context, client_ip) and returns answer string
    std::vector<BatchResult> process(
        const std::vector<BatchQuestion>& questions,
        std::function<std::string(const std::string&, const std::string&, const std::string&)> ask_fn);

    // Cancel running batch
    void cancel();

    // Get current status
    BatchStatus get_status() const;

    // Get stats
    BatchStats get_stats() const;

    // Set progress callback
    void set_progress_callback(std::function<void(size_t, size_t, size_t)> callback);

    // Get config
    const BatchConfig& get_config() const { return config_; }
};
