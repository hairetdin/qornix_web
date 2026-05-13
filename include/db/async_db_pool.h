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
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#include "async_db_driver.h"
#include "async_db_errors.h"
#include "async_db_types.h"

namespace qornix::db {

namespace net = boost::asio;

class AsyncConnectionPool;

namespace detail {

struct AsyncPooledConnection {
    std::uint64_t id{0};
    std::unique_ptr<IAsyncDatabaseDriver> driver;
    std::chrono::steady_clock::time_point created_at{};
    std::chrono::steady_clock::time_point last_used_at{};
    std::string driver_name;
};

} // namespace detail

class AsyncDbConnection {
public:
    AsyncDbConnection() = default;
    AsyncDbConnection(std::shared_ptr<AsyncConnectionPool> pool,
                      detail::AsyncPooledConnection connection);

    AsyncDbConnection(const AsyncDbConnection&) = delete;
    AsyncDbConnection& operator=(const AsyncDbConnection&) = delete;

    AsyncDbConnection(AsyncDbConnection&& other) noexcept;
    AsyncDbConnection& operator=(AsyncDbConnection&& other) noexcept;
    ~AsyncDbConnection();

    explicit operator bool() const noexcept { return static_cast<bool>(connection_.driver); }
    bool is_open() const { return connection_.driver && connection_.driver->is_open(); }
    std::string driver_name() const { return connection_.driver ? connection_.driver->driver_name() : std::string{}; }
    std::uint64_t connection_id() const noexcept { return connection_.id; }

    net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {});

    net::awaitable<QueryResult> query_one(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {});

    net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {});

    net::awaitable<void> prepare(std::string name, std::string sql, CancellationToken token = {});
    net::awaitable<void> begin(CancellationToken token = {});
    net::awaitable<void> commit(CancellationToken token = {});
    net::awaitable<void> rollback(CancellationToken token = {});

    void discard() noexcept { reusable_ = false; }

private:
    void reset();
    void throw_if_token_stopped(const CancellationToken& token) const;
    void mark_non_reusable_after_error(const DbError& error) noexcept;
    void mark_non_reusable_if_closed() noexcept;

    std::shared_ptr<AsyncConnectionPool> pool_;
    detail::AsyncPooledConnection connection_;
    bool reusable_{true};
};

class AsyncConnectionPool : public std::enable_shared_from_this<AsyncConnectionPool> {
public:
    AsyncConnectionPool(net::any_io_executor executor,
                        AsyncDriverFactory factory,
                        AsyncPoolOptions options = {},
                        std::shared_ptr<AsyncDbMetrics> metrics = {})
        : executor_(std::move(executor)),
          factory_(std::move(factory)),
          options_(std::move(options)),
          metrics_(std::move(metrics)) {
        if (!factory_) {
            throw std::invalid_argument("async DB driver factory is required");
        }
        if (options_.max_connections == 0) {
            throw std::invalid_argument("async DB pool max_connections must be greater than zero");
        }
    }

    net::awaitable<void> warmup(CancellationToken token = {}) {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (closing_) {
                    throw db_pool_rejected("database pool is closing");
                }
                if (active_ + idle_.size() >= options_.min_connections ||
                    active_ + idle_.size() >= options_.max_connections) {
                    break;
                }
            }

            auto connection = reserve_and_create_connection(std::nullopt);
            try {
                co_await connection.driver->connect(token);
                finish_successful_connect(connection);
                release(std::move(connection), true);
            } catch (...) {
                finish_failed_create();
                throw;
            }
        }
        publish_metrics();
    }

    net::awaitable<AsyncDbConnection> acquire(CancellationToken token = {}) {
        const auto remaining = token.remaining();
        const auto timeout = remaining.has_value()
            ? std::min(options_.acquire_timeout, *remaining)
            : options_.acquire_timeout;
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        if (token.is_cancelled()) {
            throw db_cancelled("database pool acquisition cancelled");
        }
        if (token.deadline_expired()) {
            throw db_timeout("database pool acquisition stopped by request deadline");
        }
        if (is_closing()) {
            throw db_pool_rejected("database pool is closing");
        }

        if (auto connection = try_take_idle(std::nullopt)) {
            co_return AsyncDbConnection{shared_from_this(), std::move(*connection)};
        }

        if (can_create_connection(std::nullopt)) {
            auto connection = reserve_and_create_connection(std::nullopt);
            try {
                co_await connection.driver->connect(token);
                finish_successful_connect(connection);
                co_return AsyncDbConnection{shared_from_this(), std::move(connection)};
            } catch (...) {
                finish_failed_create();
                throw;
            }
        }

        const auto waiter_id = enqueue_waiter();
        struct WaiterGuard {
            AsyncConnectionPool* pool;
            std::uint64_t id;
            ~WaiterGuard() { pool->remove_waiter(id); }
        } guard{this, waiter_id};

        net::steady_timer timer(executor_);
        while (std::chrono::steady_clock::now() < deadline) {
            if (token.is_cancelled()) {
                throw db_cancelled("database pool acquisition cancelled");
            }
            if (token.deadline_expired()) {
                throw db_timeout("database pool acquisition stopped by request deadline");
            }
            if (is_closing()) {
                throw db_pool_rejected("database pool is closing");
            }
            if (auto connection = try_take_idle(waiter_id)) {
                co_return AsyncDbConnection{shared_from_this(), std::move(*connection)};
            }
            if (can_create_connection(waiter_id)) {
                auto connection = reserve_and_create_connection(waiter_id);
                try {
                    co_await connection.driver->connect(token);
                    finish_successful_connect(connection);
                    co_return AsyncDbConnection{shared_from_this(), std::move(connection)};
                } catch (...) {
                    finish_failed_create();
                    throw;
                }
            }
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(net::use_awaitable);
        }

        if (token.deadline_expired()) {
            throw db_timeout("database pool acquisition stopped by request deadline");
        }
        if (metrics_) {
            metrics_->record_acquire_timeout();
        }
        throw db_pool_timeout();
    }

    void release(detail::AsyncPooledConnection connection, bool reusable) {
        if (!connection.driver) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ > 0) {
            --active_;
        }

        connection.last_used_at = now;
        const bool still_open = connection.driver->is_open();
        const bool expired = is_connection_expired_locked(connection, now);
        const bool return_to_idle = reusable && still_open && !expired && !closing_;

        if (return_to_idle) {
            idle_.push_back(std::move(connection));
        } else if (metrics_) {
            metrics_->record_discarded_connection();
            if (still_open) {
                metrics_->record_closed_connection();
            }
        }
        publish_metrics_locked();
    }

    void discard_active() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ > 0) {
            --active_;
        }
        if (metrics_) {
            metrics_->record_discarded_connection();
        }
        publish_metrics_locked();
    }

    net::awaitable<void> close() {
        std::deque<detail::AsyncPooledConnection> to_close;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing_ = true;
            to_close.swap(idle_);
            waiter_queue_.clear();
            publish_metrics_locked();
        }

        for (auto& connection : to_close) {
            if (connection.driver) {
                co_await connection.driver->close();
                if (metrics_) {
                    metrics_->record_closed_connection();
                }
            }
        }

        const auto shutdown_timeout = options_.shutdown_timeout;
        const auto deadline = std::chrono::steady_clock::now() + shutdown_timeout;
        net::steady_timer timer(executor_);
        while (active_connections() > 0 && std::chrono::steady_clock::now() < deadline) {
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(net::use_awaitable);
        }
        publish_metrics();
    }

    AsyncDbMetricsSnapshot metrics_snapshot() const {
        return metrics_ ? metrics_->snapshot() : AsyncDbMetricsSnapshot{};
    }

    std::size_t active_connections() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

    std::size_t idle_connections() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return idle_.size();
    }

    std::size_t queued_waiters() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return waiter_queue_.size();
    }

    bool is_closing() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closing_;
    }

    const AsyncPoolOptions& options() const noexcept { return options_; }

private:
    std::optional<detail::AsyncPooledConnection> try_take_idle(std::optional<std::uint64_t> waiter_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closing_ || !waiter_has_turn_locked(waiter_id)) {
            return std::nullopt;
        }

        const auto now = std::chrono::steady_clock::now();
        while (!idle_.empty()) {
            auto connection = std::move(idle_.front());
            idle_.pop_front();
            if (!connection.driver) {
                continue;
            }
            const bool still_open = !options_.validate_idle_on_acquire || connection.driver->is_open();
            const bool expired = is_connection_expired_locked(connection, now);
            if (!still_open || expired) {
                if (metrics_) {
                    metrics_->record_discarded_connection();
                    if (still_open) {
                        metrics_->record_closed_connection();
                    }
                }
                continue;
            }
            ++active_;
            publish_metrics_locked();
            return connection;
        }

        publish_metrics_locked();
        return std::nullopt;
    }

    bool can_create_connection(std::optional<std::uint64_t> waiter_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !closing_ && waiter_has_turn_locked(waiter_id) && active_ + idle_.size() < options_.max_connections;
    }

    detail::AsyncPooledConnection reserve_and_create_connection(std::optional<std::uint64_t> waiter_id) {
        std::uint64_t connection_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closing_ || !waiter_has_turn_locked(waiter_id) || active_ + idle_.size() >= options_.max_connections) {
                throw db_pool_rejected("database pool is full");
            }
            ++active_;
            connection_id = next_connection_id_++;
            publish_metrics_locked();
        }

        try {
            auto driver = factory_(executor_);
            if (!driver) {
                throw DbError(DbErrorCode::Connection, "async DB driver factory returned null");
            }
            const auto now = std::chrono::steady_clock::now();
            return detail::AsyncPooledConnection{connection_id, std::move(driver), now, now, options_.driver_name};
        } catch (...) {
            finish_failed_create();
            throw;
        }
    }

    void finish_successful_connect(detail::AsyncPooledConnection& connection) {
        if (connection.driver_name.empty() && connection.driver) {
            connection.driver_name = connection.driver->driver_name();
        }
        if (metrics_) {
            metrics_->record_created_connection();
        }
    }

    void finish_failed_create() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ > 0) {
            --active_;
        }
        if (metrics_) {
            metrics_->record_failed_connect();
        }
        publish_metrics_locked();
    }

    std::uint64_t enqueue_waiter() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closing_) {
            throw db_pool_rejected("database pool is closing");
        }
        if (waiter_queue_.size() >= options_.max_waiters) {
            if (metrics_) {
                metrics_->record_rejected_acquire();
            }
            throw db_pool_rejected();
        }
        const auto waiter_id = next_waiter_id_++;
        waiter_queue_.push_back(waiter_id);
        publish_metrics_locked();
        return waiter_id;
    }

    void remove_waiter(std::uint64_t waiter_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = std::find(waiter_queue_.begin(), waiter_queue_.end(), waiter_id);
        if (it != waiter_queue_.end()) {
            waiter_queue_.erase(it);
        }
        publish_metrics_locked();
    }

    bool waiter_has_turn_locked(std::optional<std::uint64_t> waiter_id) const {
        if (waiter_queue_.empty()) {
            return !waiter_id.has_value();
        }
        return waiter_id.has_value() && waiter_queue_.front() == *waiter_id;
    }

    bool is_connection_expired_locked(const detail::AsyncPooledConnection& connection,
                                      std::chrono::steady_clock::time_point now) const {
        if (options_.max_lifetime.count() > 0 && connection.created_at.time_since_epoch().count() > 0 &&
            now - connection.created_at >= options_.max_lifetime) {
            return true;
        }
        if (options_.idle_timeout.count() > 0 && connection.last_used_at.time_since_epoch().count() > 0 &&
            now - connection.last_used_at >= options_.idle_timeout) {
            return true;
        }
        return false;
    }

    void publish_metrics() {
        std::lock_guard<std::mutex> lock(mutex_);
        publish_metrics_locked();
    }

    void publish_metrics_locked() const {
        if (!metrics_) {
            return;
        }
        metrics_->set_active_connections(active_);
        metrics_->set_idle_connections(idle_.size());
        metrics_->set_queued_waiters(waiter_queue_.size());
    }

    net::any_io_executor executor_;
    AsyncDriverFactory factory_;
    AsyncPoolOptions options_;
    std::shared_ptr<AsyncDbMetrics> metrics_;
    mutable std::mutex mutex_;
    std::deque<detail::AsyncPooledConnection> idle_;
    std::deque<std::uint64_t> waiter_queue_;
    std::size_t active_{0};
    std::uint64_t next_connection_id_{1};
    std::uint64_t next_waiter_id_{1};
    bool closing_{false};
};

inline AsyncDbConnection::AsyncDbConnection(std::shared_ptr<AsyncConnectionPool> pool,
                                            detail::AsyncPooledConnection connection)
    : pool_(std::move(pool)), connection_(std::move(connection)) {}

inline AsyncDbConnection::AsyncDbConnection(AsyncDbConnection&& other) noexcept
    : pool_(std::move(other.pool_)), connection_(std::move(other.connection_)), reusable_(other.reusable_) {
    other.reusable_ = false;
}

inline AsyncDbConnection& AsyncDbConnection::operator=(AsyncDbConnection&& other) noexcept {
    if (this != &other) {
        reset();
        pool_ = std::move(other.pool_);
        connection_ = std::move(other.connection_);
        reusable_ = other.reusable_;
        other.reusable_ = false;
    }
    return *this;
}

inline AsyncDbConnection::~AsyncDbConnection() {
    reset();
}

inline void AsyncDbConnection::reset() {
    if (pool_ && connection_.driver) {
        pool_->release(std::move(connection_), reusable_);
    }
    pool_.reset();
    reusable_ = false;
}

inline void AsyncDbConnection::throw_if_token_stopped(const CancellationToken& token) const {
    if (token.is_cancelled()) {
        throw db_cancelled();
    }
    if (token.deadline_expired()) {
        throw db_timeout("database operation skipped because request deadline expired");
    }
}

inline void AsyncDbConnection::mark_non_reusable_after_error(const DbError& error) noexcept {
    if (error.code() == DbErrorCode::Timeout ||
        error.code() == DbErrorCode::Cancelled ||
        error.code() == DbErrorCode::Connection ||
        error.code() == DbErrorCode::ConnectionLost) {
        reusable_ = false;
        return;
    }
    mark_non_reusable_if_closed();
}

inline void AsyncDbConnection::mark_non_reusable_if_closed() noexcept {
    if (connection_.driver && !connection_.driver->is_open()) {
        reusable_ = false;
    }
}

inline net::awaitable<QueryResult> AsyncDbConnection::query(
    std::string sql,
    QueryParams params,
    QueryOptions options,
    CancellationToken token) {
    if (!connection_.driver) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    throw_if_token_stopped(token);
    try {
        co_return co_await connection_.driver->query(std::move(sql), std::move(params), std::move(options), std::move(token));
    } catch (const DbError& error) {
        mark_non_reusable_after_error(error);
        throw;
    } catch (...) {
        mark_non_reusable_if_closed();
        throw;
    }
}

inline net::awaitable<QueryResult> AsyncDbConnection::query_one(
    std::string sql,
    QueryParams params,
    QueryOptions options,
    CancellationToken token) {
    options.single_row = true;
    options.max_rows = 1;
    co_return co_await query(std::move(sql), std::move(params), std::move(options), std::move(token));
}

inline net::awaitable<QueryResult> AsyncDbConnection::execute(
    std::string sql,
    QueryParams params,
    QueryOptions options,
    CancellationToken token) {
    if (!connection_.driver) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    throw_if_token_stopped(token);
    try {
        co_return co_await connection_.driver->execute(std::move(sql), std::move(params), std::move(options), std::move(token));
    } catch (const DbError& error) {
        mark_non_reusable_after_error(error);
        throw;
    } catch (...) {
        mark_non_reusable_if_closed();
        throw;
    }
}

inline net::awaitable<void> AsyncDbConnection::prepare(std::string name, std::string sql, CancellationToken token) {
    if (!connection_.driver) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    throw_if_token_stopped(token);
    try {
        co_await connection_.driver->prepare(std::move(name), std::move(sql), std::move(token));
    } catch (const DbError& error) {
        mark_non_reusable_after_error(error);
        throw;
    } catch (...) {
        mark_non_reusable_if_closed();
        throw;
    }
}

inline net::awaitable<void> AsyncDbConnection::begin(CancellationToken token) {
    if (!connection_.driver) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    throw_if_token_stopped(token);
    try {
        co_await connection_.driver->begin(std::move(token));
    } catch (const DbError& error) {
        mark_non_reusable_after_error(error);
        throw;
    } catch (...) {
        mark_non_reusable_if_closed();
        throw;
    }
}

inline net::awaitable<void> AsyncDbConnection::commit(CancellationToken token) {
    if (!connection_.driver) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    throw_if_token_stopped(token);
    try {
        co_await connection_.driver->commit(std::move(token));
    } catch (const DbError& error) {
        mark_non_reusable_after_error(error);
        throw;
    } catch (...) {
        mark_non_reusable_if_closed();
        throw;
    }
}

inline net::awaitable<void> AsyncDbConnection::rollback(CancellationToken token) {
    if (!connection_.driver) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    throw_if_token_stopped(token);
    try {
        co_await connection_.driver->rollback(std::move(token));
    } catch (const DbError& error) {
        mark_non_reusable_after_error(error);
        throw;
    } catch (...) {
        mark_non_reusable_if_closed();
        throw;
    }
}

} // namespace qornix::db
