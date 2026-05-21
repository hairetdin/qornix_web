/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Rate Limiter — Phase 3
 *
 * Sliding window rate limiter for LLM API requests.
 * Protects against DDoS and excessive LLM usage.
 *
 * Supports per-IP and global rate limiting:
 *   - Global: max requests per second/minute across all clients
 *   - Per-IP: max requests per second/minute per client IP
 */

#include <string>
#include <deque>
#include <mutex>
#include <chrono>
#include <optional>
#include <atomic>
#include <unordered_map>

// ============================================================================
// Rate limiter configuration
// ============================================================================

struct RateLimiterConfig {
    bool enabled = false;

    // Global limits
    size_t max_requests_per_second = 10;    // max requests/sec globally
    size_t max_requests_per_minute = 100;   // max requests/min globally

    // Per-IP limits
    bool per_ip_limit = true;
    size_t max_requests_per_second_per_ip = 2;  // max requests/sec per IP
    size_t max_requests_per_minute_per_ip = 30; // max requests/min per IP

    // IP whitelist (bypass rate limiting)
    std::vector<std::string> whitelist;
};

// ============================================================================
// Rate limiter statistics
// ============================================================================

struct RateLimiterStats {
    size_t allowed = 0;
    size_t rejected = 0;
    size_t whitelisted = 0;

    double rejection_rate() const {
        size_t total = allowed + rejected;
        return total > 0 ? static_cast<double>(rejected) / total * 100.0 : 0.0;
    }
};

// ============================================================================
// Sliding window counter
// ============================================================================

class SlidingWindowCounter {
private:
    std::deque<std::chrono::steady_clock::time_point> timestamps_;
    mutable std::mutex mutex_;

public:
    // Check if within limit and record request
    bool allow(size_t max_count, std::chrono::seconds window) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        auto window_start = now - window;

        // Remove expired entries
        while (!timestamps_.empty() && timestamps_.front() < window_start) {
            timestamps_.pop_front();
        }

        if (timestamps_.size() >= max_count) {
            return false;
        }

        timestamps_.push_back(now);
        return true;
    }

    size_t count(std::chrono::seconds window) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        auto window_start = now - window;

        size_t count = 0;
        for (const auto& ts : timestamps_) {
            if (ts >= window_start) {
                count++;
            }
        }
        return count;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamps_.clear();
    }
};

// ============================================================================
// Rate limiter (global + per-IP)
// ============================================================================

class RateLimiter {
private:
    RateLimiterConfig config_;

    // Global counters
    SlidingWindowCounter global_per_second_;
    SlidingWindowCounter global_per_minute_;

    // Per-IP counters (use unique_ptr because SlidingWindowCounter has mutex)
    std::unordered_map<std::string, std::unique_ptr<SlidingWindowCounter>> ip_per_second_;
    std::unordered_map<std::string, std::unique_ptr<SlidingWindowCounter>> ip_per_minute_;
    mutable std::mutex ip_mutex_;

    // Statistics
    RateLimiterStats stats_;
    mutable std::mutex stats_mutex_;

    // Check if IP is whitelisted
    bool is_whitelisted(const std::string& ip) const {
        for (const auto& allowed : config_.whitelist) {
            if (ip == allowed) {
                return true;
            }
        }
        return false;
    }

    // Get or create per-IP counters
    SlidingWindowCounter& get_ip_second_counter(const std::string& ip) {
        std::lock_guard<std::mutex> lock(ip_mutex_);
        static const size_t MAX_TRACKED_IPS = 10000;

        auto it = ip_per_second_.find(ip);
        if (it != ip_per_second_.end()) {
            return *(it->second);
        }

        // Evict oldest if at capacity (simple round-robin by clearing)
        if (ip_per_second_.size() >= MAX_TRACKED_IPS) {
            ip_per_second_.clear();
            ip_per_minute_.clear();
        }

        auto [new_it, _] = ip_per_second_.emplace(
            ip, std::make_unique<SlidingWindowCounter>());
        ip_per_minute_.emplace(
            ip, std::make_unique<SlidingWindowCounter>());
        return *(new_it->second);
    }

    SlidingWindowCounter& get_ip_minute_counter(const std::string& ip) {
        std::lock_guard<std::mutex> lock(ip_mutex_);
        auto it = ip_per_minute_.find(ip);
        if (it != ip_per_minute_.end()) {
            return *(it->second);
        }
        // Should not happen if get_ip_second_counter is called first
        static auto empty = std::make_unique<SlidingWindowCounter>();
        return *empty;
    }

public:
    explicit RateLimiter(const RateLimiterConfig& config);
    ~RateLimiter() = default;

    // Check if request is allowed (thread-safe)
    // Returns: nullopt = allowed, some string = rejection reason
    std::optional<std::string> allow_request(const std::string& client_ip = "");

    // Get statistics
    RateLimiterStats get_stats() const;

    // Reset all counters
    void reset();

    // Check if rate limiter is available
    bool is_available() const { return config_.enabled; }
};
