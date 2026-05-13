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
#include <deque>
#include <memory>
#include <stdexcept>
#include <mutex>
#include <optional>
#include <utility>

#include "async_db_driver.h"
#include "async_db_errors.h"
#include "async_db_types.h"

namespace qornix::db {

namespace net = boost::asio;

class AsyncConnectionPool;

class AsyncDbConnection {
public:
    AsyncDbConnection() = default;
    AsyncDbConnection(std::shared_ptr<AsyncConnectionPool> pool,
                      std::unique_ptr<IAsyncDatabaseDriver> driver);

    AsyncDbConnection(const AsyncDbConnection&) = delete;
    AsyncDbConnection& operator=(const AsyncDbConnection&) = delete;

    AsyncDbConnection(AsyncDbConnection&& other) noexcept;
    AsyncDbConnection& operator=(AsyncDbConnection&& other) noexcept;
    ~AsyncDbConnection();

    explicit operator bool() const noexcept { return static_cast<bool>(driver_); }
    bool is_open() const { return driver_ && driver_->is_open(); }
    std::string driver_name() const { return driver_ ? driver_->driver_name() : std::string{}; }

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

    std::shared_ptr<AsyncConnectionPool> pool_;
    std::unique_ptr<IAsyncDatabaseDriver> driver_;
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
            std::size_t current_total = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                current_total = active_ + idle_.size();
            }
            if (current_total >= options_.min_connections || current_total >= options_.max_connections) {
                break;
            }
            auto driver = factory_(executor_);
            co_await driver->connect(token);
            release(std::move(driver), true);
        }
        publish_metrics();
    }

    net::awaitable<AsyncDbConnection> acquire(CancellationToken token = {}) {
        const auto remaining = token.remaining();
        const auto timeout = remaining.has_value()
            ? std::min(options_.acquire_timeout, *remaining)
            : options_.acquire_timeout;
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        if (auto driver = try_take_idle()) {
            co_return AsyncDbConnection{shared_from_this(), std::move(driver)};
        }

        if (can_create_connection()) {
            auto driver = reserve_and_create_driver();
            try {
                co_await driver->connect(token);
                co_return AsyncDbConnection{shared_from_this(), std::move(driver)};
            } catch (...) {
                finish_failed_create();
                throw;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (waiters_ >= options_.max_waiters) {
                if (metrics_) {
                    metrics_->record_rejected_acquire();
                }
                throw db_pool_rejected();
            }
            ++waiters_;
            publish_metrics_locked();
        }

        struct WaiterGuard {
            AsyncConnectionPool* pool;
            ~WaiterGuard() {
                std::lock_guard<std::mutex> lock(pool->mutex_);
                if (pool->waiters_ > 0) {
                    --pool->waiters_;
                }
                pool->publish_metrics_locked();
            }
        } guard{this};

        net::steady_timer timer(executor_);
        while (std::chrono::steady_clock::now() < deadline) {
            if (token.is_cancelled()) {
                throw db_cancelled("database pool acquisition cancelled");
            }
            if (auto driver = try_take_idle()) {
                co_return AsyncDbConnection{shared_from_this(), std::move(driver)};
            }
            if (can_create_connection()) {
                auto driver = reserve_and_create_driver();
                try {
                    co_await driver->connect(token);
                    co_return AsyncDbConnection{shared_from_this(), std::move(driver)};
                } catch (...) {
                    finish_failed_create();
                    throw;
                }
            }
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(net::use_awaitable);
        }

        if (metrics_) {
            metrics_->record_acquire_timeout();
        }
        throw db_pool_timeout();
    }

    void release(std::unique_ptr<IAsyncDatabaseDriver> driver, bool reusable) {
        if (!driver) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ > 0) {
            --active_;
        }
        if (reusable && driver->is_open() && !closing_) {
            idle_.push_back(std::move(driver));
        }
        publish_metrics_locked();
    }

    void discard_active() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ > 0) {
            --active_;
        }
        publish_metrics_locked();
    }

    net::awaitable<void> close() {
        std::deque<std::unique_ptr<IAsyncDatabaseDriver>> to_close;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing_ = true;
            to_close.swap(idle_);
            publish_metrics_locked();
        }
        for (auto& driver : to_close) {
            co_await driver->close();
        }
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
        return waiters_;
    }

    const AsyncPoolOptions& options() const noexcept { return options_; }

private:
    std::unique_ptr<IAsyncDatabaseDriver> try_take_idle() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (idle_.empty() || closing_) {
            return {};
        }
        auto driver = std::move(idle_.front());
        idle_.pop_front();
        ++active_;
        publish_metrics_locked();
        return driver;
    }

    bool can_create_connection() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !closing_ && active_ + idle_.size() < options_.max_connections;
    }

    std::unique_ptr<IAsyncDatabaseDriver> reserve_and_create_driver() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closing_ || active_ + idle_.size() >= options_.max_connections) {
                throw db_pool_rejected("database pool is full");
            }
            ++active_;
            publish_metrics_locked();
        }
        return factory_(executor_);
    }

    void finish_failed_create() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ > 0) {
            --active_;
        }
        publish_metrics_locked();
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
        metrics_->set_queued_waiters(waiters_);
    }

    net::any_io_executor executor_;
    AsyncDriverFactory factory_;
    AsyncPoolOptions options_;
    std::shared_ptr<AsyncDbMetrics> metrics_;
    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<IAsyncDatabaseDriver>> idle_;
    std::size_t active_{0};
    std::size_t waiters_{0};
    bool closing_{false};
};

inline AsyncDbConnection::AsyncDbConnection(std::shared_ptr<AsyncConnectionPool> pool,
                                            std::unique_ptr<IAsyncDatabaseDriver> driver)
    : pool_(std::move(pool)), driver_(std::move(driver)) {}

inline AsyncDbConnection::AsyncDbConnection(AsyncDbConnection&& other) noexcept
    : pool_(std::move(other.pool_)), driver_(std::move(other.driver_)), reusable_(other.reusable_) {
    other.reusable_ = false;
}

inline AsyncDbConnection& AsyncDbConnection::operator=(AsyncDbConnection&& other) noexcept {
    if (this != &other) {
        reset();
        pool_ = std::move(other.pool_);
        driver_ = std::move(other.driver_);
        reusable_ = other.reusable_;
        other.reusable_ = false;
    }
    return *this;
}

inline AsyncDbConnection::~AsyncDbConnection() {
    reset();
}

inline void AsyncDbConnection::reset() {
    if (pool_ && driver_) {
        pool_->release(std::move(driver_), reusable_);
    }
    pool_.reset();
    reusable_ = false;
}

inline net::awaitable<QueryResult> AsyncDbConnection::query(
    std::string sql,
    QueryParams params,
    QueryOptions options,
    CancellationToken token) {
    if (!driver_) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    if (token.is_cancelled()) {
        throw db_cancelled();
    }
    co_return co_await driver_->query(std::move(sql), std::move(params), std::move(options), std::move(token));
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
    if (!driver_) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    co_return co_await driver_->execute(std::move(sql), std::move(params), std::move(options), std::move(token));
}

inline net::awaitable<void> AsyncDbConnection::prepare(std::string name, std::string sql, CancellationToken token) {
    if (!driver_) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    co_await driver_->prepare(std::move(name), std::move(sql), std::move(token));
}

inline net::awaitable<void> AsyncDbConnection::begin(CancellationToken token) {
    if (!driver_) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    co_await driver_->begin(std::move(token));
}

inline net::awaitable<void> AsyncDbConnection::commit(CancellationToken token) {
    if (!driver_) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    co_await driver_->commit(std::move(token));
}

inline net::awaitable<void> AsyncDbConnection::rollback(CancellationToken token) {
    if (!driver_) {
        throw DbError(DbErrorCode::ConnectionLost, "async DB connection is not open");
    }
    co_await driver_->rollback(std::move(token));
}

} // namespace qornix::db
