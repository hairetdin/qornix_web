/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <cctype>
#include <utility>

#include "async_db_driver.h"
#include "async_db_errors.h"

namespace qornix::db {

namespace net = boost::asio;

class MockAsyncDriver final : public IAsyncDatabaseDriver {
public:
    explicit MockAsyncDriver(net::any_io_executor executor,
                             std::chrono::milliseconds latency = std::chrono::milliseconds{1})
        : executor_(std::move(executor)), latency_(latency) {}

    net::awaitable<void> connect(const CancellationToken& token = {}) override {
        co_await wait_or_throw(latency_, QueryOptions{}, token);
        open_ = true;
    }

    net::awaitable<void> close() override {
        open_ = false;
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

        QueryResult result;
        result.command_tag = starts_with_select(sql) ? "SELECT" : "OK";
        if (starts_with_select(sql)) {
            Row row;
            row.columns["driver"] = driver_name();
            row.columns["sql"] = sql;
            row.columns["param_count"] = std::to_string(params.size());
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
    }

    net::awaitable<void> commit(CancellationToken token = {}) override {
        try {
            co_await wait_or_throw(latency_, QueryOptions{}, token);
        } catch (const DbError& error) {
            mark_closed_after_stop(error);
            throw;
        }
        in_transaction_ = false;
    }

    net::awaitable<void> rollback(CancellationToken token = {}) override {
        try {
            co_await wait_or_throw(latency_, QueryOptions{}, token);
        } catch (const DbError& error) {
            mark_closed_after_stop(error);
            throw;
        }
        in_transaction_ = false;
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

    void mark_closed_after_stop(const DbError& error) noexcept {
        if (error.code() == DbErrorCode::Timeout || error.code() == DbErrorCode::Cancelled) {
            open_ = false;
            in_transaction_ = false;
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
    bool open_{false};
    bool in_transaction_{false};
    std::unordered_map<std::string, std::string> prepared_;
};

inline AsyncDriverFactory make_mock_async_driver_factory(
    std::chrono::milliseconds latency = std::chrono::milliseconds{1}) {
    return [latency](net::any_io_executor executor) {
        return std::make_unique<MockAsyncDriver>(std::move(executor), latency);
    };
}

} // namespace qornix::db
