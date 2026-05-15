/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <cctype>
#include <utility>

#include "async_db_driver.h"
#include "async_db_errors.h"

namespace qornix::db {

namespace net = boost::asio;

struct MockAsyncDriverStats {
    std::atomic<std::uint64_t> connects{0};
    std::atomic<std::uint64_t> closes{0};
    std::atomic<std::uint64_t> queries{0};
    std::atomic<std::uint64_t> prepares{0};
    std::atomic<std::uint64_t> begins{0};
    std::atomic<std::uint64_t> commits{0};
    std::atomic<std::uint64_t> rollbacks{0};
    std::atomic<std::uint64_t> active_transactions{0};
    mutable std::mutex last_query_mutex;
    std::string last_query_sql;
    std::size_t last_query_param_count{0};

    std::string last_sql() const {
        std::lock_guard<std::mutex> lock(last_query_mutex);
        return last_query_sql;
    }
};

class MockAsyncDriver final : public IAsyncDatabaseDriver {
public:
    explicit MockAsyncDriver(net::any_io_executor executor,
                             std::chrono::milliseconds latency = std::chrono::milliseconds{1},
                             std::shared_ptr<MockAsyncDriverStats> stats = {})
        : executor_(std::move(executor)), latency_(latency), stats_(std::move(stats)) {}

    ~MockAsyncDriver() override {
        close_transaction_counter_if_needed();
    }

    net::awaitable<void> connect(const CancellationToken& token = {}) override {
        co_await wait_or_throw(latency_, QueryOptions{}, token);
        open_ = true;
        if (stats_) {
            stats_->connects.fetch_add(1, std::memory_order_relaxed);
        }
    }

    net::awaitable<void> close() override {
        open_ = false;
        close_transaction_counter_if_needed();
        if (stats_) {
            stats_->closes.fetch_add(1, std::memory_order_relaxed);
        }
        co_return;
    }

    bool is_open() const override { return open_; }
    std::string driver_name() const override { return "mock_async"; }

    net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) override {
        if (!open_) {
            throw DbError(DbErrorCode::Connection, "mock async driver is not connected");
        }
        try {
            co_await wait_or_throw(latency_, options, token);
        } catch (const DbError& error) {
            mark_closed_after_stop(error);
            throw;
        }

        if (stats_) {
            stats_->queries.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(stats_->last_query_mutex);
            stats_->last_query_sql = sql;
            stats_->last_query_param_count = params.size();
        }

        QueryResult result;
        result.command_tag = starts_with_select(sql) ? "SELECT" : "OK";
        if (starts_with_select(sql)) {
            Row row;
            row.columns["driver"] = driver_name();
            row.columns["sql"] = sql;
            row.columns["param_count"] = std::to_string(params.size());
            row.columns["prepared"] = options.prepared ? "true" : "false";
            row.columns["statement_name"] = options.statement_name;
            for (std::size_t i = 0; i < params.size(); ++i) {
                row.columns["param_" + std::to_string(i + 1)] = params[i].is_null ? "NULL" : params[i].value;
            }
            result.rows.push_back(std::move(row));
        } else {
            result.affected_rows = 1;
        }
        co_return result;
    }


    net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) override {
        co_return co_await query(std::move(sql), std::move(params), std::move(options), std::move(token));
    }

    net::awaitable<void> prepare(std::string name, std::string sql, CancellationToken token = {}) override {
        QueryOptions options;
        try {
            co_await wait_or_throw(latency_, options, token);
        } catch (const DbError& error) {
            mark_closed_after_stop(error);
            throw;
        }
        if (stats_) {
            stats_->prepares.fetch_add(1, std::memory_order_relaxed);
        }
        prepared_[std::move(name)] = std::move(sql);
    }

    net::awaitable<void> begin(CancellationToken token = {}) override {
        try {
            co_await wait_or_throw(latency_, QueryOptions{}, token);
        } catch (const DbError& error) {
            mark_closed_after_stop(error);
            throw;
        }
        in_transaction_ = true;
        if (stats_) {
            stats_->begins.fetch_add(1, std::memory_order_relaxed);
            stats_->active_transactions.fetch_add(1, std::memory_order_relaxed);
        }
    }

    net::awaitable<void> commit(CancellationToken token = {}) override {
        try {
            co_await wait_or_throw(latency_, QueryOptions{}, token);
        } catch (const DbError& error) {
            mark_closed_after_stop(error);
            throw;
        }
        if (in_transaction_ && stats_) {
            stats_->active_transactions.fetch_sub(1, std::memory_order_relaxed);
        }
        in_transaction_ = false;
        if (stats_) {
            stats_->commits.fetch_add(1, std::memory_order_relaxed);
        }
    }

    net::awaitable<void> rollback(CancellationToken token = {}) override {
        try {
            co_await wait_or_throw(latency_, QueryOptions{}, token);
        } catch (const DbError& error) {
            mark_closed_after_stop(error);
            throw;
        }
        if (in_transaction_ && stats_) {
            stats_->active_transactions.fetch_sub(1, std::memory_order_relaxed);
        }
        in_transaction_ = false;
        if (stats_) {
            stats_->rollbacks.fetch_add(1, std::memory_order_relaxed);
        }
    }

private:
    static bool starts_with_select(std::string sql) {
        sql.erase(sql.begin(), std::find_if(sql.begin(), sql.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        std::transform(sql.begin(), sql.end(), sql.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return sql.rfind("SELECT", 0) == 0;
    }

    void close_transaction_counter_if_needed() noexcept {
        if (in_transaction_ && stats_) {
            stats_->active_transactions.fetch_sub(1, std::memory_order_relaxed);
        }
        in_transaction_ = false;
    }

    void mark_closed_after_stop(const DbError& error) noexcept {
        if (error.code() == DbErrorCode::Timeout || error.code() == DbErrorCode::Cancelled) {
            open_ = false;
            close_transaction_counter_if_needed();
        }
    }

    net::awaitable<void> wait_or_throw(std::chrono::milliseconds latency,
                                       const QueryOptions& options,
                                       const CancellationToken& token) {
        if (token.is_cancelled()) {
            throw db_cancelled();
        }
        const auto remaining = token.remaining();
        const auto effective_timeout = remaining ? std::min(options.timeout, *remaining) : options.timeout;
        if (effective_timeout.count() <= 0 || latency > effective_timeout) {
            net::steady_timer timer(executor_);
            timer.expires_after(std::max(std::chrono::milliseconds{0}, effective_timeout));
            co_await timer.async_wait(net::use_awaitable);
            throw db_timeout();
        }
        net::steady_timer timer(executor_);
        timer.expires_after(latency);
        co_await timer.async_wait(net::use_awaitable);
        if (token.is_cancelled()) {
            throw db_cancelled();
        }
    }

    net::any_io_executor executor_;
    std::chrono::milliseconds latency_;
    std::shared_ptr<MockAsyncDriverStats> stats_;
    bool open_{false};
    bool in_transaction_{false};
    std::unordered_map<std::string, std::string> prepared_;
};

inline AsyncDriverFactory make_mock_async_driver_factory(
    std::chrono::milliseconds latency = std::chrono::milliseconds{1},
    std::shared_ptr<MockAsyncDriverStats> stats = {}) {
    return [latency, stats = std::move(stats)](net::any_io_executor executor) {
        return std::make_unique<MockAsyncDriver>(std::move(executor), latency, stats);
    };
}

} // namespace qornix::db
