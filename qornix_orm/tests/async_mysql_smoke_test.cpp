/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "db/async_db.h"
#include "db/mysql_async_driver.h"

#include <boost/asio.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace net = boost::asio;

using qornix::db::AsyncDatabase;
using qornix::db::AsyncPoolOptions;
using qornix::db::MySqlAsyncDriver;
using qornix::db::MySqlAsyncOptions;
using qornix::db::QueryOptions;
using qornix::db::QueryParam;
using qornix::db::make_mysql_async_driver_factory;

namespace {

struct MySqlSmokeState {
    std::exception_ptr failure;
    bool done{false};
};

std::string getenv_string(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
}

MySqlAsyncOptions options_from_environment() {
    MySqlAsyncOptions options;
    options.connection_info = getenv_string("QORNIX_ASYNC_MYSQL_URL");
    options.host = getenv_string("QORNIX_ASYNC_MYSQL_HOST");
    options.port = getenv_string("QORNIX_ASYNC_MYSQL_PORT");
    options.username = getenv_string("QORNIX_ASYNC_MYSQL_USER");
    options.password = getenv_string("QORNIX_ASYNC_MYSQL_PASSWORD");
    options.database = getenv_string("QORNIX_ASYNC_MYSQL_DATABASE");
    if (options.host.empty()) {
        options.host = "127.0.0.1";
    }
    if (options.port.empty()) {
        options.port = "3306";
    }
    options.connect_timeout = std::chrono::milliseconds{5000};
    options.default_query_timeout = std::chrono::milliseconds{3000};
    return options;
}

bool has_mysql_config(const MySqlAsyncOptions& options) {
    return !options.connection_info.empty() || !options.username.empty();
}

std::string unexpected_result_message(const std::string& check, const qornix::db::QueryResult& result) {
    std::ostringstream out;
    out << check << "; result=" << result.to_json();
    return out.str();
}

net::awaitable<void> validate_direct_driver_paths(net::any_io_executor executor, MySqlAsyncOptions driver_options) {
    MySqlAsyncDriver driver(std::move(executor), std::move(driver_options));
    co_await driver.connect();

    co_await driver.prepare("qornix_async_mysql_smoke", "SELECT ? AS prepared_value");
    QueryOptions prepared_options;
    prepared_options.prepared = true;
    prepared_options.statement_name = "qornix_async_mysql_smoke";
    prepared_options.timeout = std::chrono::milliseconds{3000};
    qornix::db::QueryParams prepared_params;
    prepared_params.push_back(QueryParam::text("prepared"));
    auto prepared = co_await driver.query("", std::move(prepared_params), prepared_options);
    const auto prepared_value = prepared.rows.front().get("prepared_value");
    if (!prepared_value || *prepared_value != "prepared") {
        throw std::runtime_error(unexpected_result_message("unexpected async MySQL prepared statement result", prepared));
    }

    co_await driver.begin();
    auto tx_result = co_await driver.query("SELECT 'tx-ok' AS status");
    const auto status = tx_result.rows.front().get("status");
    if (!status || *status != "tx-ok") {
        throw std::runtime_error(unexpected_result_message("unexpected async MySQL transaction result", tx_result));
    }
    co_await driver.commit();
    co_await driver.close();
}

net::awaitable<void> run_mysql_smoke_test(MySqlAsyncOptions driver_options, std::shared_ptr<MySqlSmokeState> state) {
    try {
        AsyncPoolOptions pool_options;
        pool_options.min_connections = 1;
        pool_options.max_connections = 2;
        pool_options.max_waiters = 16;
        pool_options.acquire_timeout = std::chrono::milliseconds{1000};
        pool_options.query_timeout = std::chrono::milliseconds{3000};

        auto executor = co_await net::this_coro::executor;
        auto direct_driver_options = driver_options;
        auto db = std::make_shared<AsyncDatabase>(
            executor,
            make_mysql_async_driver_factory(std::move(driver_options)),
            pool_options);
        co_await db->warmup();

        QueryOptions query_options;
        query_options.timeout = std::chrono::milliseconds{3000};
        qornix::db::QueryParams params;
        params.push_back(QueryParam::text("qornix"));
        auto result = co_await db->query(
            "SELECT ? AS value, 42 AS answer",
            std::move(params),
            query_options);

        if (result.rows.size() != 1) {
            throw std::runtime_error("expected exactly one row from async MySQL smoke query");
        }
        const auto value = result.rows.front().get("value");
        const auto answer = result.rows.front().get("answer");
        if (!value || *value != "qornix") {
            throw std::runtime_error(unexpected_result_message("unexpected async MySQL parameter result", result));
        }
        if (!answer || *answer != "42") {
            throw std::runtime_error(unexpected_result_message("unexpected async MySQL numeric result", result));
        }

        co_await db->close();
        co_await validate_direct_driver_paths(executor, std::move(direct_driver_options));
    } catch (...) {
        state->failure = std::current_exception();
    }
    state->done = true;
}

} // namespace

int main() {
    auto options = options_from_environment();
    if (!has_mysql_config(options)) {
        std::cout << "QORNIX_ASYNC_MYSQL_URL or QORNIX_ASYNC_MYSQL_USER is not set; skipping async MySQL integration smoke test\n";
        return 0;
    }

    net::io_context io;
    auto state = std::make_shared<MySqlSmokeState>();
    net::co_spawn(io, run_mysql_smoke_test(std::move(options), state), net::detached);
    io.run();

    if (!state->done && !state->failure) {
        std::cerr << "async MySQL smoke test failed: coroutine did not complete\n";
        return 1;
    }
    if (state->failure) {
        try {
            std::rethrow_exception(state->failure);
        } catch (const std::exception& ex) {
            std::cerr << "async MySQL smoke test failed: " << ex.what() << "\n";
            return 1;
        }
    }

    std::cout << "async MySQL smoke test passed\n";
    return 0;
}
