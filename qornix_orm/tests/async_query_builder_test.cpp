/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "async_database_interface.h"
#include "query_builder.h"

#include <boost/asio.hpp>

#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>

namespace net = boost::asio;

namespace {

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

qornix::db::AsyncPoolOptions default_pool_options() {
    qornix::db::AsyncPoolOptions options;
    options.min_connections = 0;
    options.max_connections = 2;
    options.max_waiters = 4;
    options.acquire_timeout = std::chrono::milliseconds{50};
    options.query_timeout = std::chrono::milliseconds{100};
    return options;
}

void run_postgres_style_select(net::io_context& ioc) {
    auto db = AsyncDatabaseInterface::createMock(
        ioc.get_executor(), default_pool_options(), std::chrono::milliseconds{1});

    AsyncQueryBuilder builder(db);
    builder.setMethod("GET")
        .setTable("users")
        .addValue("id")
        .addValue("email")
        .addFilter("email='a@example.test'")
        .addOrderBy("id DESC")
        .setLimit(1)
        .prepared();

    const auto sql = builder.generateSQL();
    assert(sql == "SELECT id, email FROM users WHERE email=$1 ORDER BY id DESC LIMIT 1");
    assert(builder.hasSqlParameters());
    assert(builder.getSqlParameters().size() == 1);
    assert(builder.getSqlParameters()[0] == "a@example.test");

    auto response = run_async(ioc, builder.exec());
    assert(response.status == boost::beast::http::status::ok);
    assert(response.count == 1);

    run_async(ioc, db->close());
}

void run_mysql_style_mutations(net::io_context& ioc) {
    qornix::db::AsyncPoolOptions options = default_pool_options();
    options.driver_name = "mysql_async";
    auto stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto db = std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}, stats),
        options);

    boost::json::object create_body;
    create_body["email"] = "m@example.test";
    create_body["name"] = "Maria";

    AsyncQueryBuilder insert_builder(db);
    insert_builder.setMethod("POST").setTable("users").setData(create_body);
    const auto insert_sql = insert_builder.generateSQL();
    assert(insert_sql.find("INSERT INTO users") == 0);
    assert(insert_sql.find("?") != std::string::npos);
    assert(insert_sql.find("RETURNING") == std::string::npos);
    auto insert_response = run_async(ioc, insert_builder.exec());
    assert(insert_response.status == boost::beast::http::status::created);
    assert(insert_response.count == 1);
    assert(stats->last_sql().find("RETURNING") == std::string::npos);

    boost::json::object update_body;
    update_body["name"] = "Masha";

    AsyncQueryBuilder update_builder(db);
    update_builder.setMethod("PATCH")
        .setTable("users")
        .setData(update_body)
        .addFilter("id='9'");
    const auto update_sql = update_builder.generateSQL();
    assert(update_sql == "UPDATE users SET name = ? WHERE id=?");
    assert(update_builder.getSqlParameters().size() == 2);
    assert(update_builder.getSqlParameters()[0] == "Masha");
    assert(update_builder.getSqlParameters()[1] == "9");
    auto update_response = run_async(ioc, update_builder.exec());
    assert(update_response.status == boost::beast::http::status::ok);
    assert(update_response.count == 1);

    run_async(ioc, db->close());
}

} // namespace

int main() {
    try {
        net::io_context ioc;
        run_postgres_style_select(ioc);
        run_mysql_style_mutations(ioc);
        std::cout << "async_query_builder_test passed" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "async_query_builder_test failed: " << ex.what() << std::endl;
        return 1;
    }
}
