/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "db/async_db.h"
#include "db/postgres_async_driver.h"

#include <boost/asio.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace net = boost::asio;

using qornix::db::AsyncDatabase;
using qornix::db::AsyncPoolOptions;
using qornix::db::PostgresAsyncDriver;
using qornix::db::PostgresAsyncOptions;
using qornix::db::QueryOptions;
using qornix::db::QueryParam;
using qornix::db::make_postgres_async_driver_factory;

namespace {

std::string unexpected_result_message(const std::string& check, const qornix::db::QueryResult& result) {
    std::ostringstream out;
    out << check << "; result=" << result.to_json();
    return out.str();
}

net::awaitable<void> validate_direct_driver_paths(net::any_io_executor executor, PostgresAsyncOptions driver_options) {
    PostgresAsyncDriver driver(std::move(executor), std::move(driver_options));
    co_await driver.connect();

    co_await driver.prepare("qornix_async_smoke", "SELECT $1::text AS prepared_value");
    QueryOptions prepared_options;
    prepared_options.prepared = true;
    prepared_options.statement_name = "qornix_async_smoke";
    prepared_options.timeout = std::chrono::milliseconds{3000};
    qornix::db::QueryParams prepared_params;
    prepared_params.push_back(QueryParam::text("prepared"));
    auto prepared = co_await driver.query("", std::move(prepared_params), prepared_options);
    const auto prepared_value = prepared.rows.front().get("prepared_value");
    if (!prepared_value || *prepared_value != "prepared") {
        throw std::runtime_error(unexpected_result_message("unexpected async PostgreSQL prepared statement result", prepared));
    }

    co_await driver.begin();
    auto tx_result = co_await driver.query("SELECT 'tx-ok'::text AS status");
    const auto status = tx_result.rows.front().get("status");
    if (!status || *status != "tx-ok") {
        throw std::runtime_error(unexpected_result_message("unexpected async PostgreSQL transaction result", tx_result));
    }
    co_await driver.commit();
    co_await driver.close();
}

} // namespace

net::awaitable<void> run_smoke_test(std::string connection_info) {
    PostgresAsyncOptions driver_options;
    driver_options.connection_info = std::move(connection_info);
    driver_options.connect_timeout = std::chrono::milliseconds{5000};
    driver_options.default_query_timeout = std::chrono::milliseconds{3000};

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
        make_postgres_async_driver_factory(driver_options),
        pool_options);
    co_await db->warmup();

    QueryOptions query_options;
    query_options.timeout = std::chrono::milliseconds{3000};
    qornix::db::QueryParams params;
    params.push_back(QueryParam::text("qornix"));
    auto result = co_await db->query(
        "SELECT $1::text AS value, 42::int AS answer",
        std::move(params),
        query_options);

    if (result.rows.size() != 1) {
        throw std::runtime_error("expected exactly one row from async PostgreSQL smoke query");
    }
    const auto value = result.rows.front().get("value");
    const auto answer = result.rows.front().get("answer");
    if (!value || *value != "qornix") {
        throw std::runtime_error(unexpected_result_message("unexpected async PostgreSQL parameter result", result));
    }
    if (!answer || *answer != "42") {
        throw std::runtime_error(unexpected_result_message("unexpected async PostgreSQL numeric result", result));
    }

    co_await db->close();
    co_await validate_direct_driver_paths(executor, std::move(direct_driver_options));
}

int main() {
    const char* connection_info = std::getenv("QORNIX_ASYNC_POSTGRES_URL");
    if (!connection_info || std::string(connection_info).empty()) {
        std::cout << "QORNIX_ASYNC_POSTGRES_URL is not set; skipping async PostgreSQL integration smoke test\n";
        return 0;
    }

    try {
        net::io_context io;
        std::exception_ptr error;
        std::string connection_string(connection_info);
        net::co_spawn(io, run_smoke_test(std::move(connection_string)), [&](std::exception_ptr ex) { error = ex; });
        io.run();
        if (error) {
            std::rethrow_exception(error);
        }
    } catch (const std::exception& ex) {
        std::cerr << "async PostgreSQL smoke test failed: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "async PostgreSQL smoke test passed\n";
    return 0;
}
