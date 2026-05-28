/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "batch_processor.h"
#include <thread>
#include <iostream>
#include <algorithm>

// ============================================================================
// Constructor
// ============================================================================

BatchProcessor::BatchProcessor(const BatchConfig& config)
    : config_(config) {
}

// ============================================================================
// Worker thread
// ============================================================================

void BatchProcessor::worker_thread(
    size_t /*thread_id*/,
    const std::vector<BatchQuestion>& questions,
    size_t start_idx,
    size_t end_idx,
    std::shared_ptr<std::function<std::string(const std::string&, const std::string&, const std::string&)>> ask_fn) {

    for (size_t i = start_idx; i < end_idx && !cancelled_.load(); i++) {
        if (cancelled_.load()) break;

        auto start_time = std::chrono::steady_clock::now();

        try {
            BatchQuestion& q = const_cast<BatchQuestion&>(questions[i]);
            std::string answer = (*ask_fn)(q.question, q.context, q.client_ip);

            auto end_time = std::chrono::steady_clock::now();
            long long response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();

            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                results_[i].answer = answer;
                results_[i].success = true;
                results_[i].response_time_ms = response_time;
            }

            completed_count_.fetch_add(1, std::memory_order_relaxed);

        } catch (const std::exception& e) {
            auto end_time = std::chrono::steady_clock::now();
            long long response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();

            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                results_[i].error = e.what();
                results_[i].success = false;
                results_[i].response_time_ms = response_time;
            }

            failed_count_.fetch_add(1, std::memory_order_relaxed);

            if (config_.stop_on_error) {
                cancelled_.store(true);
                status_.store(BatchStatus::FAILED);
                break;
            }
        }

        // Notify progress
        if (progress_callback_) {
            size_t completed = completed_count_.load();
            progress_callback_(completed, questions.size(), i);
        }
    }
}

// ============================================================================
// Process batch
// ============================================================================

std::vector<BatchResult> BatchProcessor::process(
    const std::vector<BatchQuestion>& questions,
    std::function<std::string(const std::string&, const std::string&, const std::string&)> ask_fn) {

    if (questions.empty()) {
        return {};
    }

    // Reset state
    cancelled_.store(false);
    completed_count_.store(0);
    failed_count_.store(0);
    status_.store(BatchStatus::RUNNING);
    results_.clear();
    results_.resize(questions.size());

    // Initialize results with questions
    for (size_t i = 0; i < questions.size(); i++) {
        results_[i].question = questions[i].question;
        results_[i].success = false;
        results_[i].response_time_ms = 0;
    }

    // Split work across threads
    size_t num_threads = std::min(config_.max_concurrent, questions.size());
    std::vector<std::thread> threads;

    // Wrap ask_fn in a shared_ptr so worker threads can access it
    auto ask_shared = std::make_shared<std::function<
        std::string(const std::string&, const std::string&, const std::string&)>>(std::move(ask_fn));

    size_t base_chunk = questions.size() / num_threads;
    size_t remaining = questions.size() % num_threads;
    size_t start_idx = 0;

    for (size_t t = 0; t < num_threads; t++) {
        size_t chunk = base_chunk + (t < remaining ? 1 : 0);
        if (chunk == 0) break;

        size_t end_idx = start_idx + chunk;

        threads.emplace_back(&BatchProcessor::worker_thread, this,
                            t, std::cref(questions), start_idx, end_idx, ask_shared);

        start_idx = end_idx;
    }

    // Join all threads
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Finalize status
    if (!cancelled_.load()) {
        if (failed_count_.load() > 0) {
            status_.store(BatchStatus::COMPLETED); // Partially completed
        } else {
            status_.store(BatchStatus::COMPLETED);
        }
    }

    return results_;
}

// ============================================================================
// Cancel
// ============================================================================

void BatchProcessor::cancel() {
    cancelled_.store(true);
    status_.store(BatchStatus::CANCELLED);
}

// ============================================================================
// Status and stats
// ============================================================================

BatchStatus BatchProcessor::get_status() const {
    return status_.load();
}

BatchStats BatchProcessor::get_stats() const {
    BatchStats stats;
    stats.total = results_.size();
    stats.completed = completed_count_.load();
    stats.failed = failed_count_.load();

    if (status_.load() == BatchStatus::CANCELLED) {
        stats.cancelled = stats.total - stats.completed - stats.failed;
    }

    return stats;
}

void BatchProcessor::set_progress_callback(
    std::function<void(size_t, size_t, size_t)> callback) {
    progress_callback_ = std::move(callback);
}
