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
#include <future>
#include <iostream>
#include <memory>

namespace net = boost::asio;

namespace {

qornix::db::AsyncPoolOptions default_pool_options() {
    qornix::db::AsyncPoolOptions options;
    options.min_connections = 1;
    options.max_connections = 2;
    options.max_waiters = 4;
    options.acquire_timeout = std::chrono::milliseconds{50};
    options.query_timeout = std::chrono::milliseconds{100};
    return options;
}

template <typename T>
T run_async(net::io_context& ioc, net::awaitable<T> operation) {
    auto future = net::co_spawn(ioc, std::move(operation), net::use_future);
    ioc.run();
    ioc.restart();
    return future.get();
}

void run_async(net::io_context& ioc, net::awaitable<void> operation) {
    auto future = net::co_spawn(ioc, std::move(operation), net::use_future);
    ioc.run();
    ioc.restart();
    future.get();
}

void run_delay(net::io_context& ioc, std::chrono::milliseconds delay) {
    net::steady_timer timer(ioc);
    timer.expires_after(delay);
    auto future = timer.async_wait(net::use_future);
    ioc.run();
    ioc.restart();
    future.get();
}

void run_basic_query_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    auto db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options, std::chrono::milliseconds{1});
    run_async(ioc, db->warmup());
    std::atomic_signal_fence(std::memory_order_seq_cst);

    qornix::db::QueryParams query_params;
    query_params.push_back(qornix::db::QueryParam::text("42"));
    auto result = run_async(
        ioc,
        db->queryOne("SELECT * FROM users WHERE id = $1", std::move(query_params)));
    assert(result.size() == 1);
    assert(result.one().get("driver").value_or("") == "mock_async");
    assert(result.one().get("param_1").value_or("") == "42");

    auto table = db->table("users");
    auto table_result = run_async(ioc, table.findById("7"));
    assert(table_result.size() == 1);
    assert(table_result.one().get("param_1").value_or("") == "7");

    assert(qornix::db::sql_placeholder_for_driver("postgres_async", 1) == "$1");
    assert(qornix::db::sql_placeholder_for_driver("mysql_async", 1) == "?");
    assert(qornix::db::sql_driver_supports_returning("postgres_async"));
    assert(!qornix::db::sql_driver_supports_returning("mysql_async"));
    assert(db->placeholder(2) == "$2");
    assert(db->supportsReturning());

    auto snapshot = db->metricsSnapshot();
    assert(snapshot.query_success >= 2);
    assert(snapshot.created_connections >= 1);
    assert(snapshot.query_latency_p50_us > 0);

    run_async(ioc, db->close());
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

void run_async_table_filter_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    auto db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options, std::chrono::milliseconds{1});

    qornix::db::QueryOptions table_options;
    table_options.prepared = true;
    auto table = db->table("users");
    table.filter({{"status", "active"}, {"tenant_id", "acme"}})
        .select({"id", "email"})
        .order_by("id DESC")
        .limit(5)
        .queryOptions(table_options);
    auto all_result = run_async(ioc, table.all());
    assert(all_result.size() == 1);
    assert(all_result.one().get("sql").value_or("") ==
           "SELECT id, email FROM users WHERE status = $1 AND tenant_id = $2 ORDER BY id DESC LIMIT 5");
    assert(all_result.one().get("prepared").value_or("") == "true");
    assert(all_result.one().get("param_1").value_or("") == "active");
    assert(all_result.one().get("param_2").value_or("") == "acme");

    auto find_table = db->table("users");
    auto optional_by_id = run_async(ioc, find_table.findOptionalById("7"));
    assert(optional_by_id.has_value());

    auto create_table = db->table("users");
    auto created = run_async(
        ioc,
        create_table.create({{"email", "a@example.test"}, {"name", "Alice"}}));
    assert(created.affected_rows == 1 || created.size() == 1);

    run_async(ioc, db->close());
}

void run_mysql_style_async_table_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    auto mysql_stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    qornix::db::AsyncPoolOptions mysql_style_options = options;
    mysql_style_options.min_connections = 0;
    mysql_style_options.driver_name = "mysql_async";
    auto mysql_style_db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}, mysql_stats),
        mysql_style_options);

    assert(mysql_style_db->placeholder(1) == "?");
    assert(!mysql_style_db->supportsReturning());

    auto find_table = mysql_style_db->table("users");
    auto mysql_find = run_async(ioc, find_table.findById("9"));
    assert(mysql_find.one().get("sql").value_or("") == "SELECT * FROM users WHERE id = ? LIMIT 1");

    auto insert_table = mysql_style_db->table("users");
    auto mysql_insert = run_async(
        ioc,
        insert_table.insert({{"email", "m@example.test"}, {"name", "Maria"}}));
    assert(mysql_insert.affected_rows == 1);
    assert(mysql_stats->last_sql() == "INSERT INTO users (email, name) VALUES (?, ?)");

    auto update_table = mysql_style_db->table("users");
    auto mysql_update = run_async(ioc, update_table.update("9", {{"name", "Masha"}}));
    assert(mysql_update.affected_rows == 1);
    assert(mysql_stats->last_sql() == "UPDATE users SET name = ? WHERE id = ?");

    auto delete_table = mysql_style_db->table("users");
    auto mysql_delete = run_async(ioc, delete_table.delete_("9"));
    assert(mysql_delete.affected_rows == 1);
    assert(mysql_stats->last_sql() == "DELETE FROM users WHERE id = ?");

    run_async(ioc, mysql_style_db->close());
}

void run_prepared_statement_cache_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    qornix::db::AsyncPoolOptions prepared_options_pool = options;
    prepared_options_pool.min_connections = 0;
    prepared_options_pool.max_connections = 1;
    prepared_options_pool.prepared_cache_size = 2;

    auto prepared_stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto prepared_db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}, prepared_stats),
        prepared_options_pool);

    qornix::db::QueryOptions use_prepared;
    use_prepared.prepared = true;

    qornix::db::QueryParams prepared_params_a;
    prepared_params_a.push_back(qornix::db::QueryParam::text("a"));
    auto prepared_first = run_async(
        ioc,
        prepared_db->queryOne("SELECT * FROM prepared WHERE id = $1", std::move(prepared_params_a), use_prepared));
    assert(prepared_first.size() == 1);
    const auto statement_name = prepared_first.one().get("statement_name").value_or("");
    assert(prepared_first.one().get("prepared").value_or("") == "true");
    assert(!statement_name.empty());
    assert(prepared_stats->prepares.load(std::memory_order_relaxed) == 1);

    qornix::db::QueryParams prepared_params_b;
    prepared_params_b.push_back(qornix::db::QueryParam::text("b"));
    auto prepared_second = run_async(
        ioc,
        prepared_db->queryOne("SELECT * FROM prepared WHERE id = $1", std::move(prepared_params_b), use_prepared));
    assert(prepared_second.one().get("statement_name").value_or("") == statement_name);
    assert(prepared_stats->prepares.load(std::memory_order_relaxed) == 1);

    auto prepared_snapshot = prepared_db->metricsSnapshot();
    assert(prepared_snapshot.prepared_cache_misses == 1);
    assert(prepared_snapshot.prepared_cache_hits == 1);

    auto optional_row = run_async(ioc, prepared_db->queryOptional("SELECT optional_row", {}, use_prepared));
    assert(optional_row.has_value());
    auto optional_none = run_async(ioc, prepared_db->queryOptional("UPDATE optional_row SET value = 1"));
    assert(!optional_none.has_value());
    auto returning_result = run_async(ioc, prepared_db->executeReturning("SELECT returning_row", {}, use_prepared));
    assert(returning_result.size() == 1);

    run_async(ioc, prepared_db->close());
}

void run_prepared_cache_eviction_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    qornix::db::AsyncPoolOptions evict_options = options;
    evict_options.min_connections = 0;
    evict_options.max_connections = 1;
    evict_options.prepared_cache_size = 1;

    qornix::db::QueryOptions use_prepared;
    use_prepared.prepared = true;

    auto evict_stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto evict_db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}, evict_stats),
        evict_options);

    auto evict_a1 = run_async(ioc, evict_db->queryOne("SELECT evict_a", {}, use_prepared));
    auto evict_b = run_async(ioc, evict_db->queryOne("SELECT evict_b", {}, use_prepared));
    auto evict_a2 = run_async(ioc, evict_db->queryOne("SELECT evict_a", {}, use_prepared));
    (void)evict_a1;
    (void)evict_b;
    (void)evict_a2;

    assert(evict_stats->prepares.load(std::memory_order_relaxed) == 3);
    auto evict_snapshot = evict_db->metricsSnapshot();
    assert(evict_snapshot.prepared_cache_misses == 3);
    assert(evict_snapshot.prepared_cache_evictions == 2);

    run_async(ioc, evict_db->close());
}

void run_prepared_cache_reconnect_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    qornix::db::AsyncPoolOptions reconnect_options = options;
    reconnect_options.min_connections = 0;
    reconnect_options.max_connections = 1;
    reconnect_options.prepared_cache_size = 2;

    qornix::db::QueryOptions use_prepared;
    use_prepared.prepared = true;

    auto reconnect_stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto reconnect_db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}, reconnect_stats),
        reconnect_options);

    {
        auto connection = run_async(ioc, reconnect_db->acquire());
        auto cached_name = run_async(ioc, connection.prepare_cached("SELECT reconnect_prepared"));
        assert(!cached_name.empty());
        connection.discard();
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    auto reconnect_result = run_async(ioc, reconnect_db->queryOne("SELECT reconnect_prepared", {}, use_prepared));
    assert(reconnect_result.size() == 1);
    assert(reconnect_stats->prepares.load(std::memory_order_relaxed) == 2);

    run_async(ioc, reconnect_db->close());
}

void run_transaction_commit_and_rollback_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    auto quick_db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options, std::chrono::milliseconds{1});
    {
        auto tx = run_async(ioc, quick_db->beginTransaction());
        auto tx_result = run_async(ioc, tx.query("SELECT 1"));
        assert(tx_result.size() == 1);
        run_async(ioc, tx.commit());
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    run_async(ioc, quick_db->close());

    qornix::db::AsyncPoolOptions tx_options = options;
    tx_options.min_connections = 0;
    tx_options.max_connections = 1;
    tx_options.query_timeout = std::chrono::milliseconds{50};

    auto tx_stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto tx_db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}, tx_stats),
        tx_options);

    {
        auto tx = run_async(ioc, tx_db->beginTransaction());
        assert(tx.active());
        assert(tx.connection_id() == 1);
        auto tx_result = run_async(ioc, tx.query_one("SELECT tx_commit"));
        assert(tx_result.size() == 1);
        run_async(ioc, tx.commit());
        assert(!tx.active());

        bool rejected_after_commit = false;
        try {
            auto ignored = run_async(ioc, tx.query("SELECT after_commit"));
            (void)ignored;
        } catch (const qornix::db::DbError& error) {
            rejected_after_commit = error.code() == qornix::db::DbErrorCode::QueryRejected;
        }
        assert(rejected_after_commit);
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    assert(tx_stats->begins.load(std::memory_order_relaxed) == 1);
    assert(tx_stats->commits.load(std::memory_order_relaxed) == 1);
    assert(tx_stats->active_transactions.load(std::memory_order_relaxed) == 0);
    assert(tx_db->metricsSnapshot().idle_connections == 1);

    {
        auto tx = run_async(ioc, tx_db->beginTransaction());
        auto tx_result = run_async(ioc, tx.execute("UPDATE tx_rollback SET value = 1"));
        assert(tx_result.affected_rows == 1);
        run_async(ioc, tx.rollback());
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    assert(tx_stats->rollbacks.load(std::memory_order_relaxed) == 1);
    assert(tx_stats->active_transactions.load(std::memory_order_relaxed) == 0);

    run_async(ioc, tx_db->close());
}

void run_transaction_abandon_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    qornix::db::AsyncPoolOptions tx_options = options;
    tx_options.min_connections = 0;
    tx_options.max_connections = 1;
    tx_options.query_timeout = std::chrono::milliseconds{50};

    auto tx_stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto tx_db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}, tx_stats),
        tx_options);

    {
        auto tx = run_async(ioc, tx_db->beginTransaction());
        auto tx_result = run_async(ioc, tx.query("SELECT auto_rollback"));
        assert(tx_result.size() == 1);
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    for (int i = 0; i < 20 && tx_stats->rollbacks.load(std::memory_order_relaxed) < 1; ++i) {
        run_delay(ioc, std::chrono::milliseconds{1});
    }
    assert(tx_stats->rollbacks.load(std::memory_order_relaxed) == 1);
    assert(tx_stats->active_transactions.load(std::memory_order_relaxed) == 0);
    assert(tx_db->metricsSnapshot().discarded_connections >= 1);

    run_async(ioc, tx_db->close());
}

void run_transaction_failure_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    qornix::db::AsyncPoolOptions tx_options = options;
    tx_options.min_connections = 0;
    tx_options.max_connections = 1;
    tx_options.query_timeout = std::chrono::milliseconds{50};

    auto tx_cancel_stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto tx_cancel_db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}, tx_cancel_stats),
        tx_options);
    bool tx_cancelled = false;
    {
        auto tx = run_async(ioc, tx_cancel_db->beginTransaction());
        qornix::db::CancellationToken tx_cancel_token;
        tx_cancel_token.cancel();
        try {
            qornix::db::QueryParams cancelled_tx_params;
            qornix::db::QueryOptions cancelled_tx_options;
            auto ignored = run_async(
                ioc,
                tx.query("SELECT tx_cancelled", std::move(cancelled_tx_params), cancelled_tx_options, tx_cancel_token));
            (void)ignored;
        } catch (const qornix::db::DbError& error) {
            tx_cancelled = error.code() == qornix::db::DbErrorCode::Cancelled;
        }
        assert(tx_cancelled);
        assert(!tx.active());
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    assert(tx_cancel_stats->active_transactions.load(std::memory_order_relaxed) == 0);
    assert(tx_cancel_db->metricsSnapshot().discarded_connections >= 1);
    {
        auto replacement = run_async(ioc, tx_cancel_db->acquire());
        assert(replacement.connection_id() >= 2);
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    run_async(ioc, tx_cancel_db->close());

    auto tx_timeout_stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto tx_timeout_db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{25}, tx_timeout_stats),
        tx_options);
    bool tx_timed_out = false;
    {
        auto tx = run_async(ioc, tx_timeout_db->beginTransaction());
        try {
            qornix::db::QueryOptions tx_timeout_options;
            tx_timeout_options.timeout = std::chrono::milliseconds{1};
            qornix::db::QueryParams timeout_tx_params;
            auto ignored = run_async(
                ioc,
                tx.query("SELECT tx_timeout", std::move(timeout_tx_params), tx_timeout_options));
            (void)ignored;
        } catch (const qornix::db::DbError& error) {
            tx_timed_out = error.code() == qornix::db::DbErrorCode::Timeout;
        }
        assert(tx_timed_out);
        assert(!tx.active());
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    assert(tx_timeout_stats->active_transactions.load(std::memory_order_relaxed) == 0);
    assert(tx_timeout_db->metricsSnapshot().discarded_connections >= 1);
    {
        auto replacement = run_async(ioc, tx_timeout_db->acquire());
        assert(replacement.connection_id() >= 2);
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    run_async(ioc, tx_timeout_db->close());
}

void run_query_timeout_and_cancel_test(net::io_context& ioc, const qornix::db::AsyncPoolOptions& options) {
    bool timed_out = false;
    auto slow = AsyncDatabaseInterface::createMock(ioc.get_executor(), options, std::chrono::milliseconds{25});
    try {
        qornix::db::QueryOptions slow_options;
        slow_options.timeout = std::chrono::milliseconds{1};
        qornix::db::QueryParams slow_params;
        auto slow_result = run_async(ioc, slow->query("SELECT pg_sleep(1)", std::move(slow_params), slow_options));
        (void)slow_result;
    } catch (const qornix::db::DbError& error) {
        timed_out = error.code() == qornix::db::DbErrorCode::Timeout;
    }
    assert(timed_out);
    auto slow_snapshot = slow->metricsSnapshot();
    assert(slow_snapshot.query_timeout == 1);
    assert(slow_snapshot.query_option_timeouts == 1);
    assert(slow_snapshot.request_deadline_timeouts == 0);
    run_async(ioc, slow->close());

    bool pool_default_timeout = false;
    qornix::db::AsyncPoolOptions default_timeout_options = options;
    default_timeout_options.query_timeout = std::chrono::milliseconds{1};
    auto pool_default = AsyncDatabaseInterface::createMock(
        ioc.get_executor(), default_timeout_options, std::chrono::milliseconds{25});
    try {
        qornix::db::QueryParams default_timeout_params;
        auto ignored = run_async(ioc, pool_default->query("SELECT default_timeout", std::move(default_timeout_params)));
        (void)ignored;
    } catch (const qornix::db::DbError& error) {
        pool_default_timeout = error.code() == qornix::db::DbErrorCode::Timeout;
    }
    assert(pool_default_timeout);
    auto pool_default_snapshot = pool_default->metricsSnapshot();
    assert(pool_default_snapshot.query_timeout == 1);
    assert(pool_default_snapshot.query_pool_default_timeouts == 1);
    run_async(ioc, pool_default->close());

    bool request_deadline_timeout = false;
    auto request_deadline_db = AsyncDatabaseInterface::createMock(
        ioc.get_executor(), options, std::chrono::milliseconds{25});
    qornix::db::CancellationToken request_token;
    request_token.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1};
    try {
        qornix::db::QueryOptions request_options;
        request_options.timeout = std::chrono::milliseconds{100};
        qornix::db::QueryParams request_params;
        auto ignored = run_async(
            ioc,
            request_deadline_db->query(
                "SELECT request_deadline", std::move(request_params), request_options, request_token));
        (void)ignored;
    } catch (const qornix::db::DbError& error) {
        request_deadline_timeout = error.code() == qornix::db::DbErrorCode::Timeout;
    }
    assert(request_deadline_timeout);
    auto request_deadline_snapshot = request_deadline_db->metricsSnapshot();
    assert(request_deadline_snapshot.query_timeout == 1);
    assert(request_deadline_snapshot.request_deadline_timeouts == 1);
    run_async(ioc, request_deadline_db->close());

    bool cancelled = false;
    auto cancelled_db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options, std::chrono::milliseconds{1});
    qornix::db::CancellationToken cancelled_token;
    cancelled_token.cancel();
    try {
        qornix::db::QueryParams cancelled_params;
        auto ignored = run_async(
            ioc,
            cancelled_db->query("SELECT cancelled", std::move(cancelled_params), {}, cancelled_token));
        (void)ignored;
    } catch (const qornix::db::DbError& error) {
        cancelled = error.code() == qornix::db::DbErrorCode::Cancelled;
    }
    assert(cancelled);
    auto cancelled_snapshot = cancelled_db->metricsSnapshot();
    assert(cancelled_snapshot.query_cancelled == 1);
    assert(cancelled_snapshot.query_timeout == 0);
    run_async(ioc, cancelled_db->close());
}

void run_pool_acquire_timeout_test(net::io_context& ioc) {
    bool pool_acquire_timeout = false;
    qornix::db::AsyncPoolOptions pool_timeout_options;
    pool_timeout_options.max_connections = 1;
    pool_timeout_options.max_waiters = 1;
    pool_timeout_options.acquire_timeout = std::chrono::milliseconds{1};
    pool_timeout_options.query_timeout = std::chrono::milliseconds{100};
    auto pool_timeout_db = AsyncDatabaseInterface::createMock(
        ioc.get_executor(), pool_timeout_options, std::chrono::milliseconds{1});
    {
        auto held_connection = run_async(ioc, pool_timeout_db->acquire());
        try {
            qornix::db::QueryParams timeout_params;
            auto ignored = run_async(ioc, pool_timeout_db->query("SELECT waits_for_pool", std::move(timeout_params)));
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

    run_async(ioc, pool_timeout_db->close());
}

void run_config_parsing_test() {
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
    Config::getInstance().set("db.pool.prepared_cache_size", "7");

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
    assert(parsed_options.prepared_cache_size == 7);
}

void run_pool_hardening_test(net::io_context& ioc) {
    qornix::db::AsyncPoolOptions hardened_options;
    hardened_options.min_connections = 1;
    hardened_options.max_connections = 1;
    hardened_options.max_waiters = 1;
    hardened_options.acquire_timeout = std::chrono::milliseconds{20};
    hardened_options.query_timeout = std::chrono::milliseconds{100};
    hardened_options.idle_timeout = std::chrono::milliseconds{1};
    hardened_options.max_lifetime = std::chrono::milliseconds{1000};
    hardened_options.shutdown_timeout = std::chrono::milliseconds{5};

    auto hardened = AsyncDatabaseInterface::createMock(ioc.get_executor(), hardened_options, std::chrono::milliseconds{1});
    run_async(ioc, hardened->warmup());
    std::atomic_signal_fence(std::memory_order_seq_cst);

    auto warm_snapshot = hardened->metricsSnapshot();
    assert(warm_snapshot.idle_connections == 1);
    assert(warm_snapshot.created_connections == 1);

    run_delay(ioc, std::chrono::milliseconds{3});

    {
        auto refreshed_connection = run_async(ioc, hardened->acquire());
        std::atomic_signal_fence(std::memory_order_seq_cst);
        assert(refreshed_connection.connection_id() >= 2);
    }

    auto hardened_snapshot = hardened->metricsSnapshot();
    assert(hardened_snapshot.created_connections >= 2);
    assert(hardened_snapshot.discarded_connections >= 1);

    run_async(ioc, hardened->close());
    std::atomic_signal_fence(std::memory_order_seq_cst);

    bool rejected_after_close = false;
    try {
        auto ignored = run_async(ioc, hardened->acquire());
        (void)ignored;
    } catch (const qornix::db::DbError& error) {
        rejected_after_close = error.code() == qornix::db::DbErrorCode::PoolRejected;
    }
    assert(rejected_after_close);
}

void run_mock_test(net::io_context& ioc) {
    const auto options = default_pool_options();

    run_basic_query_test(ioc, options);
    run_async_table_filter_test(ioc, options);
    run_mysql_style_async_table_test(ioc, options);
    run_prepared_statement_cache_test(ioc, options);
    run_prepared_cache_eviction_test(ioc, options);
    run_prepared_cache_reconnect_test(ioc, options);
    run_transaction_commit_and_rollback_test(ioc, options);
    run_transaction_abandon_test(ioc, options);
    run_transaction_failure_test(ioc, options);
    run_query_timeout_and_cancel_test(ioc, options);
    run_pool_acquire_timeout_test(ioc);
    run_config_parsing_test();
    run_pool_hardening_test(ioc);
}

} // namespace

int main() {
    try {
        net::io_context ioc;
        run_mock_test(ioc);
        std::cout << "async_db_mock_test passed" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "async_db_mock_test failed: " << ex.what() << std::endl;
        return 1;
    }
}
