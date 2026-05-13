/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "async_database_interface.h"
#include "async_table_manager.h"

#include <boost/asio.hpp>

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

        auto tx = co_await db->beginTransaction();
        auto tx_result = co_await tx.query("SELECT 1");
        assert(tx_result.size() == 1);
        co_await tx.commit();

        bool timed_out = false;
        try {
            qornix::db::QueryOptions slow_options;
            slow_options.timeout = std::chrono::milliseconds{1};
            auto slow = AsyncDatabaseInterface::createMock(executor, options, std::chrono::milliseconds{25});
            qornix::db::QueryParams slow_params;
            auto slow_result = co_await slow->query("SELECT pg_sleep(1)", std::move(slow_params), slow_options);
            (void)slow_result;
        } catch (const qornix::db::DbError& error) {
            timed_out = error.code() == qornix::db::DbErrorCode::Timeout;
        }
        assert(timed_out);

        auto snapshot = db->metricsSnapshot();
        assert(snapshot.query_success >= 2);

        co_await db->close();
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
