/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "async_database_interface.h"
#include "async_table_manager.h"

#include <boost/asio.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>

namespace net = boost::asio;

namespace {

struct MockTestState {
    std::exception_ptr failure;
    bool done{false};
};


net::awaitable<void> run_mock_test(std::shared_ptr<MockTestState> state) {
    try {
        qornix::db::AsyncPoolOptions options;
        options.min_connections = 1;
        options.max_connections = 2;
        options.max_waiters = 4;
        options.acquire_timeout = std::chrono::milliseconds{50};
        options.query_timeout = std::chrono::milliseconds{100};

        auto executor = co_await net::this_coro::executor;
        auto db = AsyncDatabaseInterface::createMock(executor, options, std::chrono::milliseconds{1});
        co_await db->warmup();
        std::atomic_signal_fence(std::memory_order_seq_cst);

        qornix::db::QueryParams query_params;
        query_params.push_back(qornix::db::QueryParam::text("42"));
        auto result = co_await db->queryOne(
            "SELECT * FROM users WHERE id = $1",
            std::move(query_params));
        assert(result.size() == 1);
        assert(result.one().get("driver").value_or("") == "mock_async");
        assert(result.one().get("param_1").value_or("") == "42");

        auto table_result = co_await db->table("users").findById("7");
        assert(table_result.size() == 1);
        assert(table_result.one().get("param_1").value_or("") == "7");

        {
            auto tx = co_await db->beginTransaction();
            auto tx_result = co_await tx.query("SELECT 1");
            assert(tx_result.size() == 1);
            co_await tx.commit();
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }

        bool timed_out = false;
        auto slow = AsyncDatabaseInterface::createMock(executor, options, std::chrono::milliseconds{25});
        try {
            qornix::db::QueryOptions slow_options;
            slow_options.timeout = std::chrono::milliseconds{1};
            qornix::db::QueryParams slow_params;
            auto slow_result = co_await slow->query("SELECT pg_sleep(1)", std::move(slow_params), slow_options);
            (void)slow_result;
        } catch (const qornix::db::DbError& error) {
            timed_out = error.code() == qornix::db::DbErrorCode::Timeout;
        }
        assert(timed_out);
        auto slow_snapshot = slow->metricsSnapshot();
        assert(slow_snapshot.query_timeout == 1);
        assert(slow_snapshot.query_option_timeouts == 1);
        assert(slow_snapshot.request_deadline_timeouts == 0);

        bool pool_default_timeout = false;
        qornix::db::AsyncPoolOptions default_timeout_options = options;
        default_timeout_options.query_timeout = std::chrono::milliseconds{1};
        auto pool_default = AsyncDatabaseInterface::createMock(
            executor, default_timeout_options, std::chrono::milliseconds{25});
        try {
            qornix::db::QueryParams default_timeout_params;
            auto ignored = co_await pool_default->query("SELECT default_timeout", std::move(default_timeout_params));
            (void)ignored;
        } catch (const qornix::db::DbError& error) {
            pool_default_timeout = error.code() == qornix::db::DbErrorCode::Timeout;
        }
        assert(pool_default_timeout);
        auto pool_default_snapshot = pool_default->metricsSnapshot();
        assert(pool_default_snapshot.query_timeout == 1);
        assert(pool_default_snapshot.query_pool_default_timeouts == 1);

        bool request_deadline_timeout = false;
        auto request_deadline_db = AsyncDatabaseInterface::createMock(
            executor, options, std::chrono::milliseconds{25});
        qornix::db::CancellationToken request_token;
        request_token.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1};
        try {
            qornix::db::QueryOptions request_options;
            request_options.timeout = std::chrono::milliseconds{100};
            qornix::db::QueryParams request_params;
            auto ignored = co_await request_deadline_db->query(
                "SELECT request_deadline", std::move(request_params), request_options, request_token);
            (void)ignored;
        } catch (const qornix::db::DbError& error) {
            request_deadline_timeout = error.code() == qornix::db::DbErrorCode::Timeout;
        }
        assert(request_deadline_timeout);
        auto request_deadline_snapshot = request_deadline_db->metricsSnapshot();
        assert(request_deadline_snapshot.query_timeout == 1);
        assert(request_deadline_snapshot.request_deadline_timeouts == 1);

        bool cancelled = false;
        auto cancelled_db = AsyncDatabaseInterface::createMock(executor, options, std::chrono::milliseconds{1});
        qornix::db::CancellationToken cancelled_token;
        cancelled_token.cancel();
        try {
            qornix::db::QueryParams cancelled_params;
            auto ignored = co_await cancelled_db->query(
                "SELECT cancelled", std::move(cancelled_params), {}, cancelled_token);
            (void)ignored;
        } catch (const qornix::db::DbError& error) {
            cancelled = error.code() == qornix::db::DbErrorCode::Cancelled;
        }
        assert(cancelled);
        auto cancelled_snapshot = cancelled_db->metricsSnapshot();
        assert(cancelled_snapshot.query_cancelled == 1);
        assert(cancelled_snapshot.query_timeout == 0);

        bool pool_acquire_timeout = false;
        qornix::db::AsyncPoolOptions pool_timeout_options;
        pool_timeout_options.max_connections = 1;
        pool_timeout_options.max_waiters = 1;
        pool_timeout_options.acquire_timeout = std::chrono::milliseconds{1};
        pool_timeout_options.query_timeout = std::chrono::milliseconds{100};
        auto pool_timeout_db = AsyncDatabaseInterface::createMock(
            executor, pool_timeout_options, std::chrono::milliseconds{1});
        {
            auto held_connection = co_await pool_timeout_db->acquire();
            try {
                qornix::db::QueryParams timeout_params;
                auto ignored = co_await pool_timeout_db->query(
                    "SELECT waits_for_pool", std::move(timeout_params));
                (void)ignored;
            } catch (const qornix::db::DbError& error) {
                pool_acquire_timeout = error.code() == qornix::db::DbErrorCode::PoolTimeout;
            }
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
        assert(pool_acquire_timeout);
        auto pool_timeout_snapshot = pool_timeout_db->metricsSnapshot();
        assert(pool_timeout_snapshot.acquire_timeouts == 1);
        assert(pool_timeout_snapshot.query_timeout == 0);
        assert(pool_timeout_snapshot.query_error == 1);

        auto snapshot = db->metricsSnapshot();
        assert(snapshot.query_success >= 2);
        assert(snapshot.created_connections >= 1);
        assert(snapshot.query_latency_p50_us > 0);

        Config::getInstance().reset();
        Config::getInstance().set("db.driver", "mock_async");
        Config::getInstance().set("db.pool.name", "mock_pool");
        Config::getInstance().set("db.pool.min_connections", "1");
        Config::getInstance().set("db.pool.max_connections", "3");
        Config::getInstance().set("db.pool.max_waiters", "9");
        Config::getInstance().set("db.pool.acquire_timeout_ms", "11");
        Config::getInstance().set("db.pool.query_timeout_ms", "22");
        Config::getInstance().set("db.pool.idle_timeout_ms", "33");
        Config::getInstance().set("db.pool.max_lifetime_ms", "44");
        Config::getInstance().set("db.pool.health_check_interval_ms", "55");
        Config::getInstance().set("db.pool.shutdown_timeout_ms", "66");
        auto parsed_options = AsyncDatabaseInterface::poolOptionsFromConfig();
        assert(parsed_options.pool_name == "mock_pool");
        assert(parsed_options.driver_name == "mock_async");
        assert(parsed_options.min_connections == 1);
        assert(parsed_options.max_connections == 3);
        assert(parsed_options.max_waiters == 9);
        assert(parsed_options.acquire_timeout == std::chrono::milliseconds{11});
        assert(parsed_options.query_timeout == std::chrono::milliseconds{22});
        assert(parsed_options.idle_timeout == std::chrono::milliseconds{33});
        assert(parsed_options.max_lifetime == std::chrono::milliseconds{44});
        assert(parsed_options.health_check_interval == std::chrono::milliseconds{55});
        assert(parsed_options.shutdown_timeout == std::chrono::milliseconds{66});

        qornix::db::AsyncPoolOptions hardened_options;
        hardened_options.min_connections = 1;
        hardened_options.max_connections = 1;
        hardened_options.max_waiters = 1;
        hardened_options.acquire_timeout = std::chrono::milliseconds{20};
        hardened_options.query_timeout = std::chrono::milliseconds{100};
        hardened_options.idle_timeout = std::chrono::milliseconds{1};
        hardened_options.max_lifetime = std::chrono::milliseconds{1000};
        hardened_options.shutdown_timeout = std::chrono::milliseconds{5};
        auto hardened = AsyncDatabaseInterface::createMock(executor, hardened_options, std::chrono::milliseconds{1});
        co_await hardened->warmup();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        auto warm_snapshot = hardened->metricsSnapshot();
        assert(warm_snapshot.idle_connections == 1);
        assert(warm_snapshot.created_connections == 1);

        net::steady_timer idle_timer(executor);
        idle_timer.expires_after(std::chrono::milliseconds{3});
        co_await idle_timer.async_wait(net::use_awaitable);
        {
            auto refreshed_connection = co_await hardened->acquire();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            assert(refreshed_connection.connection_id() >= 2);
        }
        auto hardened_snapshot = hardened->metricsSnapshot();
        assert(hardened_snapshot.created_connections >= 2);
        assert(hardened_snapshot.discarded_connections >= 1);

        co_await hardened->close();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        bool rejected_after_close = false;
        try {
            auto ignored = co_await hardened->acquire();
            (void)ignored;
        } catch (const qornix::db::DbError& error) {
            rejected_after_close = error.code() == qornix::db::DbErrorCode::PoolRejected;
        }
        assert(rejected_after_close);

        co_await db->close();
        std::atomic_signal_fence(std::memory_order_seq_cst);
    } catch (...) {
        state->failure = std::current_exception();
    }

    state->done = true;
}

} // namespace

int main() {
    net::io_context ioc;
    auto state = std::make_shared<MockTestState>();

    net::co_spawn(ioc, run_mock_test(state), net::detached);

    ioc.run();

    if (!state->done && !state->failure) {
        std::cerr << "async_db_mock_test failed: coroutine did not complete" << std::endl;
        return 1;
    }

    if (state->failure) {
        try {
            std::rethrow_exception(state->failure);
        } catch (const std::exception& ex) {
            std::cerr << "async_db_mock_test failed: " << ex.what() << std::endl;
            return 1;
        }
    }

    std::cout << "async_db_mock_test passed" << std::endl;
    return 0;
}
