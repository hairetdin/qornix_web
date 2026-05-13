/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <utility>

#include "async_db_pool.h"

namespace qornix::db {

namespace net = boost::asio;

class AsyncTransaction {
public:
    AsyncTransaction() = default;
    explicit AsyncTransaction(AsyncDbConnection connection) : connection_(std::move(connection)) {}

    AsyncTransaction(const AsyncTransaction&) = delete;
    AsyncTransaction& operator=(const AsyncTransaction&) = delete;
    AsyncTransaction(AsyncTransaction&&) noexcept = default;
    AsyncTransaction& operator=(AsyncTransaction&&) noexcept = default;

    ~AsyncTransaction() {
        // If user forgets to commit, the connection is discarded so a dirty
        // transaction cannot be returned to the pool. Explicit rollback is
        // still preferred because destructors cannot co_await.
        if (connection_ && !finished_) {
            connection_.discard();
        }
    }

    net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        co_return co_await connection_.query(std::move(sql), std::move(params), std::move(options), std::move(token));
    }

    net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        co_return co_await connection_.execute(std::move(sql), std::move(params), std::move(options), std::move(token));
    }

    net::awaitable<void> commit(CancellationToken token = {}) {
        co_await connection_.commit(std::move(token));
        finished_ = true;
    }

    net::awaitable<void> rollback(CancellationToken token = {}) {
        co_await connection_.rollback(std::move(token));
        finished_ = true;
    }

private:
    AsyncDbConnection connection_;
    bool finished_{false};
};

class AsyncDatabase {
public:
    AsyncDatabase(net::any_io_executor executor,
                  AsyncDriverFactory factory,
                  AsyncPoolOptions options = {},
                  std::shared_ptr<AsyncDbMetrics> metrics = std::make_shared<AsyncDbMetrics>())
        : metrics_(std::move(metrics)),
          pool_(std::make_shared<AsyncConnectionPool>(std::move(executor),
                                                      std::move(factory),
                                                      std::move(options),
                                                      metrics_)) {}

    net::awaitable<void> warmup(CancellationToken token = {}) {
        co_await pool_->warmup(std::move(token));
    }

    net::awaitable<AsyncDbConnection> acquire(CancellationToken token = {}) {
        co_return co_await pool_->acquire(std::move(token));
    }

    net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        if (metrics_) { metrics_->record_query_started(); }
        auto conn = co_await acquire(token);
        try {
            auto result = co_await conn.query(std::move(sql), std::move(params), std::move(options), std::move(token));
            if (metrics_) { metrics_->record_query_success(); }
            co_return result;
        } catch (const DbError& error) {
            if (metrics_) {
                if (error.code() == DbErrorCode::Timeout || error.code() == DbErrorCode::PoolTimeout) { metrics_->record_query_timeout(); }
                else if (error.code() == DbErrorCode::Cancelled) { metrics_->record_query_cancelled(); }
                else { metrics_->record_query_error(); }
            }
            throw;
        }
    }

    net::awaitable<QueryResult> query_one(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        options.single_row = true;
        options.max_rows = 1;
        if (metrics_) { metrics_->record_query_started(); }
        auto conn = co_await acquire(token);
        try {
            auto result = co_await conn.query(std::move(sql), std::move(params), std::move(options), std::move(token));
            if (metrics_) { metrics_->record_query_success(); }
            co_return result;
        } catch (const DbError& error) {
            if (metrics_) {
                if (error.code() == DbErrorCode::Timeout || error.code() == DbErrorCode::PoolTimeout) { metrics_->record_query_timeout(); }
                else if (error.code() == DbErrorCode::Cancelled) { metrics_->record_query_cancelled(); }
                else { metrics_->record_query_error(); }
            }
            throw;
        }
    }

    net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        if (metrics_) { metrics_->record_query_started(); }
        auto conn = co_await acquire(token);
        try {
            auto result = co_await conn.execute(std::move(sql), std::move(params), std::move(options), std::move(token));
            if (metrics_) { metrics_->record_query_success(); }
            co_return result;
        } catch (const DbError& error) {
            if (metrics_) {
                if (error.code() == DbErrorCode::Timeout || error.code() == DbErrorCode::PoolTimeout) { metrics_->record_query_timeout(); }
                else if (error.code() == DbErrorCode::Cancelled) { metrics_->record_query_cancelled(); }
                else { metrics_->record_query_error(); }
            }
            throw;
        }
    }

    net::awaitable<AsyncTransaction> begin_transaction(CancellationToken token = {}) {
        auto conn = co_await acquire(token);
        co_await conn.begin(token);
        co_return AsyncTransaction{std::move(conn)};
    }

    net::awaitable<void> close() {
        co_await pool_->close();
    }

    std::shared_ptr<AsyncConnectionPool> pool() const noexcept { return pool_; }
    AsyncDbMetricsSnapshot metrics_snapshot() const { return pool_->metrics_snapshot(); }

private:
    std::shared_ptr<AsyncDbMetrics> metrics_;
    std::shared_ptr<AsyncConnectionPool> pool_;
};

} // namespace qornix::db
