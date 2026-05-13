/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace qornix::db {

struct CancellationToken {
    std::shared_ptr<std::atomic_bool> cancelled{std::make_shared<std::atomic_bool>(false)};
    std::optional<std::chrono::steady_clock::time_point> deadline;

    bool is_cancelled() const noexcept {
        return cancelled && cancelled->load(std::memory_order_relaxed);
    }

    bool deadline_expired() const {
        return deadline && *deadline <= std::chrono::steady_clock::now();
    }

    void cancel() const noexcept {
        if (cancelled) {
            cancelled->store(true, std::memory_order_relaxed);
        }
    }

    std::optional<std::chrono::milliseconds> remaining() const {
        if (!deadline) {
            return std::nullopt;
        }
        const auto now = std::chrono::steady_clock::now();
        if (*deadline <= now) {
            return std::chrono::milliseconds{0};
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now);
    }
};

struct QueryParam {
    std::string value;
    bool is_null{false};

    static QueryParam text(std::string v) { return QueryParam{std::move(v), false}; }
    static QueryParam null() { return QueryParam{"", true}; }
};

using QueryParams = std::vector<QueryParam>;

struct Row {
    std::unordered_map<std::string, std::string> columns;

    bool contains(const std::string& name) const {
        return columns.find(name) != columns.end();
    }

    std::optional<std::string> get(const std::string& name) const {
        const auto it = columns.find(name);
        if (it == columns.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    boost::json::object to_json() const {
        boost::json::object object;
        for (const auto& [key, value] : columns) {
            object[key] = value;
        }
        return object;
    }
};

struct QueryResult {
    std::vector<Row> rows;
    std::uint64_t affected_rows{0};
    std::string command_tag;

    bool empty() const noexcept { return rows.empty(); }
    std::size_t size() const noexcept { return rows.size(); }

    const Row& one() const {
        if (rows.empty()) {
            throw std::out_of_range("query result has no rows");
        }
        return rows.front();
    }

    boost::json::value to_json_value() const {
        boost::json::object root;
        root["affected_rows"] = affected_rows;
        root["command_tag"] = command_tag;
        boost::json::array data;
        for (const auto& row : rows) {
            data.push_back(row.to_json());
        }
        root["data"] = std::move(data);
        return root;
    }

    std::string to_json() const {
        return boost::json::serialize(to_json_value());
    }
};

enum class DbTimeoutSource {
    None,
    QueryOption,
    PoolDefault,
    RequestDeadline
};

inline const char* to_string(DbTimeoutSource source) {
    switch (source) {
        case DbTimeoutSource::None: return "none";
        case DbTimeoutSource::QueryOption: return "query_option";
        case DbTimeoutSource::PoolDefault: return "pool_default";
        case DbTimeoutSource::RequestDeadline: return "request_deadline";
    }
    return "none";
}

struct QueryOptions {
    std::chrono::milliseconds timeout{std::chrono::milliseconds{2000}};
    DbTimeoutSource timeout_source{DbTimeoutSource::QueryOption};
    bool single_row{false};
    std::size_t max_rows{0};
    bool prepared{false};
    std::string statement_name;
};

struct AsyncPoolOptions {
    std::string pool_name{"default"};
    std::string driver_name;
    std::size_t min_connections{0};
    std::size_t max_connections{32};
    std::size_t max_waiters{1024};
    std::chrono::milliseconds acquire_timeout{std::chrono::milliseconds{200}};
    std::chrono::milliseconds query_timeout{std::chrono::milliseconds{2000}};
    std::chrono::milliseconds idle_timeout{std::chrono::milliseconds{30000}};
    std::chrono::milliseconds max_lifetime{std::chrono::milliseconds{1800000}};
    std::chrono::milliseconds health_check_interval{std::chrono::milliseconds{10000}};
    std::chrono::milliseconds shutdown_timeout{std::chrono::milliseconds{5000}};
    bool validate_idle_on_acquire{true};
};

struct AsyncDbMetricsSnapshot {
    std::uint64_t active_connections{0};
    std::uint64_t idle_connections{0};
    std::uint64_t queued_waiters{0};
    std::uint64_t rejected_acquires{0};
    std::uint64_t acquire_timeouts{0};
    std::uint64_t created_connections{0};
    std::uint64_t closed_connections{0};
    std::uint64_t discarded_connections{0};
    std::uint64_t failed_connects{0};
    std::uint64_t query_total{0};
    std::uint64_t query_success{0};
    std::uint64_t query_error{0};
    std::uint64_t query_timeout{0};
    std::uint64_t query_cancelled{0};
    std::uint64_t query_option_timeouts{0};
    std::uint64_t query_pool_default_timeouts{0};
    std::uint64_t request_deadline_timeouts{0};
    std::uint64_t query_latency_p50_us{0};
    std::uint64_t query_latency_p95_us{0};
    std::uint64_t query_latency_p99_us{0};
};

class AsyncDbMetrics {
public:
    void set_active_connections(std::uint64_t value) { active_connections_.store(value, std::memory_order_relaxed); }
    void set_idle_connections(std::uint64_t value) { idle_connections_.store(value, std::memory_order_relaxed); }
    void set_queued_waiters(std::uint64_t value) { queued_waiters_.store(value, std::memory_order_relaxed); }
    void record_rejected_acquire() { rejected_acquires_.fetch_add(1, std::memory_order_relaxed); }
    void record_acquire_timeout() { acquire_timeouts_.fetch_add(1, std::memory_order_relaxed); }
    void record_created_connection() { created_connections_.fetch_add(1, std::memory_order_relaxed); }
    void record_closed_connection() { closed_connections_.fetch_add(1, std::memory_order_relaxed); }
    void record_discarded_connection() { discarded_connections_.fetch_add(1, std::memory_order_relaxed); }
    void record_failed_connect() { failed_connects_.fetch_add(1, std::memory_order_relaxed); }
    void record_query_started() { query_total_.fetch_add(1, std::memory_order_relaxed); }
    void record_query_success() { query_success_.fetch_add(1, std::memory_order_relaxed); }
    void record_query_error() { query_error_.fetch_add(1, std::memory_order_relaxed); }
    void record_query_timeout() { query_timeout_.fetch_add(1, std::memory_order_relaxed); }
    void record_query_cancelled() { query_cancelled_.fetch_add(1, std::memory_order_relaxed); }
    void record_query_option_timeout() { query_option_timeouts_.fetch_add(1, std::memory_order_relaxed); }
    void record_query_pool_default_timeout() { query_pool_default_timeouts_.fetch_add(1, std::memory_order_relaxed); }
    void record_request_deadline_timeout() { request_deadline_timeouts_.fetch_add(1, std::memory_order_relaxed); }

    void record_query_latency(std::chrono::microseconds latency) {
        const auto value = static_cast<std::uint64_t>(std::max<std::int64_t>(0, latency.count()));
        std::lock_guard<std::mutex> lock(latency_mutex_);
        query_latencies_us_.push_back(value);
        if (query_latencies_us_.size() > max_latency_samples_) {
            query_latencies_us_.erase(query_latencies_us_.begin());
        }
    }

    AsyncDbMetricsSnapshot snapshot() const {
        AsyncDbMetricsSnapshot result;
        result.active_connections = active_connections_.load(std::memory_order_relaxed);
        result.idle_connections = idle_connections_.load(std::memory_order_relaxed);
        result.queued_waiters = queued_waiters_.load(std::memory_order_relaxed);
        result.rejected_acquires = rejected_acquires_.load(std::memory_order_relaxed);
        result.acquire_timeouts = acquire_timeouts_.load(std::memory_order_relaxed);
        result.created_connections = created_connections_.load(std::memory_order_relaxed);
        result.closed_connections = closed_connections_.load(std::memory_order_relaxed);
        result.discarded_connections = discarded_connections_.load(std::memory_order_relaxed);
        result.failed_connects = failed_connects_.load(std::memory_order_relaxed);
        result.query_total = query_total_.load(std::memory_order_relaxed);
        result.query_success = query_success_.load(std::memory_order_relaxed);
        result.query_error = query_error_.load(std::memory_order_relaxed);
        result.query_timeout = query_timeout_.load(std::memory_order_relaxed);
        result.query_cancelled = query_cancelled_.load(std::memory_order_relaxed);
        result.query_option_timeouts = query_option_timeouts_.load(std::memory_order_relaxed);
        result.query_pool_default_timeouts = query_pool_default_timeouts_.load(std::memory_order_relaxed);
        result.request_deadline_timeouts = request_deadline_timeouts_.load(std::memory_order_relaxed);
        auto latency_samples = copy_latency_samples();
        if (!latency_samples.empty()) {
            std::sort(latency_samples.begin(), latency_samples.end());
            result.query_latency_p50_us = percentile(latency_samples, 50);
            result.query_latency_p95_us = percentile(latency_samples, 95);
            result.query_latency_p99_us = percentile(latency_samples, 99);
        }
        return result;
    }

private:
    std::vector<std::uint64_t> copy_latency_samples() const {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        return query_latencies_us_;
    }

    static std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, std::uint64_t percentile_value) {
        if (sorted.empty()) {
            return 0;
        }
        const auto numerator = percentile_value * (sorted.size() - 1);
        const auto index = static_cast<std::size_t>((numerator + 99) / 100);
        return sorted[std::min(index, sorted.size() - 1)];
    }

    std::atomic<std::uint64_t> active_connections_{0};
    std::atomic<std::uint64_t> idle_connections_{0};
    std::atomic<std::uint64_t> queued_waiters_{0};
    std::atomic<std::uint64_t> rejected_acquires_{0};
    std::atomic<std::uint64_t> acquire_timeouts_{0};
    std::atomic<std::uint64_t> created_connections_{0};
    std::atomic<std::uint64_t> closed_connections_{0};
    std::atomic<std::uint64_t> discarded_connections_{0};
    std::atomic<std::uint64_t> failed_connects_{0};
    std::atomic<std::uint64_t> query_total_{0};
    std::atomic<std::uint64_t> query_success_{0};
    std::atomic<std::uint64_t> query_error_{0};
    std::atomic<std::uint64_t> query_timeout_{0};
    std::atomic<std::uint64_t> query_cancelled_{0};
    std::atomic<std::uint64_t> query_option_timeouts_{0};
    std::atomic<std::uint64_t> query_pool_default_timeouts_{0};
    std::atomic<std::uint64_t> request_deadline_timeouts_{0};
    static constexpr std::size_t max_latency_samples_{1024};
    mutable std::mutex latency_mutex_;
    std::vector<std::uint64_t> query_latencies_us_;
};

} // namespace qornix::db
