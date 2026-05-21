/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rate_limiter.h"
#include <iostream>

// ============================================================================
// Rate limiter implementation
// ============================================================================

RateLimiter::RateLimiter(const RateLimiterConfig& config)
    : config_(config) {}

std::optional<std::string> RateLimiter::allow_request(const std::string& client_ip) {
    if (!config_.enabled) {
        return std::nullopt; // Rate limiting disabled
    }

    // Check whitelist
    if (!client_ip.empty() && is_whitelisted(client_ip)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.whitelisted++;
        stats_.allowed++;
        return std::nullopt;
    }

    // Check global rate limits
    if (!global_per_second_.allow(config_.max_requests_per_second,
                                   std::chrono::seconds(1))) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.rejected++;
        return "Rate limit exceeded: too many requests per second (global)";
    }

    if (!global_per_minute_.allow(config_.max_requests_per_minute,
                                   std::chrono::seconds(60))) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.rejected++;
        return "Rate limit exceeded: too many requests per minute (global)";
    }

    // Check per-IP rate limits
    if (config_.per_ip_limit && !client_ip.empty()) {
        auto& ip_second = get_ip_second_counter(client_ip);
        auto& ip_minute = get_ip_minute_counter(client_ip);

        if (!ip_second.allow(config_.max_requests_per_second_per_ip,
                              std::chrono::seconds(1))) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.rejected++;
            return "Rate limit exceeded: too many requests per second (per-IP)";
        }

        if (!ip_minute.allow(config_.max_requests_per_minute_per_ip,
                              std::chrono::seconds(60))) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.rejected++;
            return "Rate limit exceeded: too many requests per minute (per-IP)";
        }
    }

    // Request allowed
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.allowed++;
    return std::nullopt;
}

RateLimiterStats RateLimiter::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void RateLimiter::reset() {
    global_per_second_.clear();
    global_per_minute_.clear();

    {
        std::lock_guard<std::mutex> lock(ip_mutex_);
        ip_per_second_.clear();
        ip_per_minute_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = RateLimiterStats{};
    }
}
