/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "async_db_driver.h"

namespace qornix::db {

namespace net = boost::asio;

struct MySqlAsyncOptions {
    // Optional DSN. Supported form: mysql://user:password@host:port/database
    // Query string parameters are currently ignored.
    std::string connection_info;

    // Explicit fields override values parsed from connection_info when non-empty.
    std::string host{"127.0.0.1"};
    std::string port{"3306"};
    std::string username;
    std::string password;
    std::string database;

    std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{5000}};
    std::chrono::milliseconds default_query_timeout{std::chrono::milliseconds{2000}};
};

class MySqlAsyncDriver final : public IAsyncDatabaseDriver {
public:
    MySqlAsyncDriver(net::any_io_executor executor, MySqlAsyncOptions options);
    ~MySqlAsyncDriver() override;

    MySqlAsyncDriver(const MySqlAsyncDriver&) = delete;
    MySqlAsyncDriver& operator=(const MySqlAsyncDriver&) = delete;

    net::awaitable<void> connect(const CancellationToken& token = {}) override;
    net::awaitable<void> close() override;
    bool is_open() const override;
    std::string driver_name() const override;

    net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) override;

    net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) override;

    net::awaitable<void> prepare(
        std::string name,
        std::string sql,
        CancellationToken token = {}) override;

    net::awaitable<void> begin(CancellationToken token = {}) override;
    net::awaitable<void> commit(CancellationToken token = {}) override;
    net::awaitable<void> rollback(CancellationToken token = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

AsyncDriverFactory make_mysql_async_driver_factory(MySqlAsyncOptions options);

} // namespace qornix::db
