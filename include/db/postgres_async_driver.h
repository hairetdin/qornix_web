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

struct PostgresAsyncOptions {
    std::string connection_info;
    std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{5000}};
    std::chrono::milliseconds default_query_timeout{std::chrono::milliseconds{2000}};
};

class PostgresAsyncDriver final : public IAsyncDatabaseDriver {
public:
    PostgresAsyncDriver(net::any_io_executor executor, PostgresAsyncOptions options);
    ~PostgresAsyncDriver() override;

    PostgresAsyncDriver(const PostgresAsyncDriver&) = delete;
    PostgresAsyncDriver& operator=(const PostgresAsyncDriver&) = delete;

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

AsyncDriverFactory make_postgres_async_driver_factory(PostgresAsyncOptions options);

} // namespace qornix::db
