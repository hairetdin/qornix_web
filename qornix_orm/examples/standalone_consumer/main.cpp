/*
 * qornix_orm standalone consumer smoke sample.
 *
 * This file deliberately uses only qornix_orm public headers and the exported
 * qornix::orm target.  It is meant to be built from this directory, outside the
 * top-level qornix_web build, to validate standalone consumption.
 */

#include "async_database_interface.h"
#include "async_table_manager.h"

#include <boost/asio.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace net = boost::asio;
using namespace std::chrono_literals;

namespace {

net::awaitable<void> run_smoke() {
    auto executor = co_await net::this_coro::executor;

    qornix::db::AsyncPoolOptions options;
    options.pool_name = "standalone-consumer";
    options.driver_name = "mock_async";
    options.min_connections = 1;
    options.max_connections = 2;
    options.max_waiters = 8;
    options.acquire_timeout = 100ms;
    options.query_timeout = 500ms;

    auto db = AsyncDatabaseInterface::createMock(executor, options, 1ms);
    co_await db->warmup();

    qornix::db::QueryParams params;
    params.push_back(qornix::db::QueryParam::text("42"));

    auto one = co_await db->queryOne(
        "SELECT * FROM users WHERE id = $1",
        std::move(params));

    if (one.size() != 1) {
        throw std::runtime_error("expected one row from mock async query");
    }

    if (one.one().get("driver").value_or("") != "mock_async") {
        throw std::runtime_error("mock async driver marker was not returned");
    }

    auto users = db->table("users");
    auto table_result = co_await users.filter("status", "active")
                                      .select({"id", "email"})
                                      .limit(1)
                                      .prepared(true)
                                      .findAll();

    if (table_result.empty()) {
        throw std::runtime_error("expected table manager result from mock async query");
    }

    auto snapshot = db->metricsSnapshot();
    if (snapshot.query_success < 2) {
        throw std::runtime_error("expected async DB success metrics to be recorded");
    }

    std::cout << "qornix_orm standalone async smoke passed"
              << " queries=" << snapshot.query_success
              << " created_connections=" << snapshot.created_connections
              << '\n';

    co_await db->close();
}

} // namespace

int main() {
    try {
        net::io_context ioc;
        auto future = net::co_spawn(ioc, run_smoke(), net::use_future);
        ioc.run();
        future.get();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "qornix_orm standalone async smoke failed: " << e.what() << '\n';
        return 1;
    }
}
