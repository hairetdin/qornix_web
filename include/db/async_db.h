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
#include <utility>
#include <type_traits>

#include "async_db_pool.h"

namespace qornix::db {

namespace net = boost::asio;

class AsyncTransaction {
public:
    enum class State {
        Empty,
        Active,
        Committed,
        RolledBack,
        Failed,
        Abandoned
    };

    AsyncTransaction() = default;
    explicit AsyncTransaction(AsyncDbConnection connection)
        : connection_(std::move(connection)), state_(State::Active) {}

    AsyncTransaction(const AsyncTransaction&) = delete;
    AsyncTransaction& operator=(const AsyncTransaction&) = delete;

    AsyncTransaction(AsyncTransaction&& other) noexcept
        : connection_(std::move(other.connection_)), state_(other.state_) {
        other.state_ = State::Empty;
    }

    AsyncTransaction& operator=(AsyncTransaction&& other) noexcept {
        if (this != &other) {
            abandon_unfinished();
            connection_ = std::move(other.connection_);
            state_ = other.state_;
            other.state_ = State::Empty;
        }
        return *this;
    }

    ~AsyncTransaction() {
        abandon_unfinished();
    }

    explicit operator bool() const noexcept { return active(); }
    bool active() const noexcept { return connection_ && state_ == State::Active; }
    State state() const noexcept { return state_; }
    std::uint64_t connection_id() const noexcept { return connection_.connection_id(); }

    net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        ensure_active();
        try {
            co_return co_await connection_.query(std::move(sql), std::move(params), std::move(options), std::move(token));
        } catch (const DbError& error) {
            handle_operation_error(error);
            throw;
        } catch (...) {
            connection_.discard();
            state_ = State::Failed;
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
        co_return co_await query(std::move(sql), std::move(params), std::move(options), std::move(token));
    }

    net::awaitable<std::optional<Row>> query_optional(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        auto result = co_await query_one(std::move(sql), std::move(params), std::move(options), std::move(token));
        co_return result.optional_one();
    }

    net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        ensure_active();
        try {
            co_return co_await connection_.execute(std::move(sql), std::move(params), std::move(options), std::move(token));
        } catch (const DbError& error) {
            handle_operation_error(error);
            throw;
        } catch (...) {
            connection_.discard();
            state_ = State::Failed;
            throw;
        }
    }

    net::awaitable<QueryResult> execute_returning(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        co_return co_await execute(std::move(sql), std::move(params), std::move(options), std::move(token));
    }

    net::awaitable<void> commit(CancellationToken token = {}) {
        ensure_active();
        try {
            co_await connection_.commit(std::move(token));
            state_ = State::Committed;
            connection_.release();
        } catch (const DbError& error) {
            handle_operation_error(error);
            throw;
        } catch (...) {
            connection_.discard();
            state_ = State::Failed;
            throw;
        }
    }

    net::awaitable<void> rollback(CancellationToken token = {}) {
        ensure_active();
        try {
            co_await connection_.rollback(std::move(token));
            state_ = State::RolledBack;
            connection_.release();
        } catch (const DbError& error) {
            handle_operation_error(error);
            throw;
        } catch (...) {
            connection_.discard();
            state_ = State::Failed;
            throw;
        }
    }

    void discard() noexcept {
        if (connection_) {
            connection_.discard();
            connection_.release();
        }
        state_ = State::Failed;
    }

private:
    void ensure_active() const {
        if (!connection_ || state_ != State::Active) {
            throw DbError(DbErrorCode::QueryRejected, "async DB transaction is not active");
        }
    }

    static bool is_connection_fatal(const DbError& error) noexcept {
        return error.code() == DbErrorCode::Timeout ||
               error.code() == DbErrorCode::Cancelled ||
               error.code() == DbErrorCode::Connection ||
               error.code() == DbErrorCode::ConnectionLost;
    }

    void handle_operation_error(const DbError& error) {
        if (is_connection_fatal(error)) {
            if (connection_) {
                connection_.discard();
                connection_.release();
            }
            state_ = State::Failed;
        }
    }

    void abandon_unfinished() noexcept {
        if (connection_ && state_ == State::Active) {
            state_ = State::Abandoned;
            connection_.rollback_abandoned();
        }
    }

    AsyncDbConnection connection_;
    State state_{State::Empty};
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
        auto start = std::chrono::steady_clock::now();
        auto effective_options = resolve_query_options(std::move(options), token);
        if (metrics_) { metrics_->record_query_started(); }
        try {
            throw_if_token_stopped(token);
            auto conn = co_await acquire(token);
            auto result = co_await conn.query(std::move(sql), std::move(params), effective_options, std::move(token));
            record_query_latency(start);
            if (metrics_) { metrics_->record_query_success(); }
            co_return result;
        } catch (const DbError& error) {
            record_query_latency(start);
            record_query_error(error, effective_options.timeout_source, token);
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
        auto start = std::chrono::steady_clock::now();
        auto effective_options = resolve_query_options(std::move(options), token);
        if (metrics_) { metrics_->record_query_started(); }
        try {
            throw_if_token_stopped(token);
            auto conn = co_await acquire(token);
            auto result = co_await conn.query(std::move(sql), std::move(params), effective_options, std::move(token));
            record_query_latency(start);
            if (metrics_) { metrics_->record_query_success(); }
            co_return result;
        } catch (const DbError& error) {
            record_query_latency(start);
            record_query_error(error, effective_options.timeout_source, token);
            throw;
        }
    }

    net::awaitable<std::optional<Row>> query_optional(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        auto result = co_await query_one(std::move(sql), std::move(params), std::move(options), std::move(token));
        co_return result.optional_one();
    }

    net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        auto start = std::chrono::steady_clock::now();
        auto effective_options = resolve_query_options(std::move(options), token);
        if (metrics_) { metrics_->record_query_started(); }
        try {
            throw_if_token_stopped(token);
            auto conn = co_await acquire(token);
            auto result = co_await conn.execute(std::move(sql), std::move(params), effective_options, std::move(token));
            record_query_latency(start);
            if (metrics_) { metrics_->record_query_success(); }
            co_return result;
        } catch (const DbError& error) {
            record_query_latency(start);
            record_query_error(error, effective_options.timeout_source, token);
            throw;
        }
    }

    net::awaitable<QueryResult> execute_returning(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) {
        co_return co_await execute(std::move(sql), std::move(params), std::move(options), std::move(token));
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
    const AsyncPoolOptions& options() const noexcept { return pool_->options(); }
    std::string driver_name() const { return pool_->options().driver_name; }
    SqlPlaceholderStyle placeholder_style() const { return placeholder_style_for_driver(driver_name()); }
    std::string placeholder(std::size_t one_based_index) const {
        return sql_placeholder(placeholder_style(), one_based_index);
    }
    bool supports_returning() const { return sql_driver_supports_returning(driver_name()); }
    AsyncDbMetricsSnapshot metrics_snapshot() const { return pool_->metrics_snapshot(); }

private:
    QueryOptions resolve_query_options(QueryOptions options, const CancellationToken& token) const {
        const auto query_timeout = options.timeout;
        options.timeout_source = query_timeout.count() > 0 ? DbTimeoutSource::QueryOption : DbTimeoutSource::None;

        const auto pool_timeout = pool_->options().query_timeout;
        if (pool_timeout.count() > 0 &&
            (options.timeout.count() <= 0 || pool_timeout < options.timeout)) {
            options.timeout = pool_timeout;
            options.timeout_source = DbTimeoutSource::PoolDefault;
        }

        if (const auto remaining = token.remaining()) {
            if (options.timeout.count() <= 0 || *remaining < options.timeout) {
                options.timeout = *remaining;
                options.timeout_source = DbTimeoutSource::RequestDeadline;
            }
        }

        return options;
    }

    static void throw_if_token_stopped(const CancellationToken& token) {
        if (token.is_cancelled()) {
            throw db_cancelled();
        }
        if (token.deadline_expired()) {
            throw db_timeout("database query skipped because request deadline expired");
        }
    }

    void record_query_latency(std::chrono::steady_clock::time_point start) const {
        if (!metrics_) {
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        metrics_->record_query_latency(elapsed);
    }

    void record_query_error(const DbError& error,
                            DbTimeoutSource timeout_source,
                            const CancellationToken& token) const {
        if (!metrics_) {
            return;
        }
        if (error.code() == DbErrorCode::Timeout) {
            metrics_->record_query_timeout();
            if (timeout_source == DbTimeoutSource::RequestDeadline || token.deadline_expired()) {
                metrics_->record_request_deadline_timeout();
            } else if (timeout_source == DbTimeoutSource::PoolDefault) {
                metrics_->record_query_pool_default_timeout();
            } else if (timeout_source == DbTimeoutSource::QueryOption) {
                metrics_->record_query_option_timeout();
            }
        } else if (error.code() == DbErrorCode::Cancelled) {
            metrics_->record_query_cancelled();
        } else {
            metrics_->record_query_error();
        }
    }

    std::shared_ptr<AsyncDbMetrics> metrics_;
    std::shared_ptr<AsyncConnectionPool> pool_;
};

} // namespace qornix::db
