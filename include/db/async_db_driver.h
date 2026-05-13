/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <functional>
#include <memory>
#include <string>

#include "async_db_types.h"

namespace qornix::db {

namespace net = boost::asio;

class IAsyncDatabaseDriver {
public:
    virtual ~IAsyncDatabaseDriver() = default;

    virtual net::awaitable<void> connect(const CancellationToken& token = {}) = 0;
    virtual net::awaitable<void> close() = 0;
    virtual bool is_open() const = 0;
    virtual std::string driver_name() const = 0;

    virtual net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) = 0;

    virtual net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) = 0;

    virtual net::awaitable<void> prepare(
        std::string name,
        std::string sql,
        CancellationToken token = {}) = 0;

    virtual net::awaitable<void> begin(CancellationToken token = {}) = 0;
    virtual net::awaitable<void> commit(CancellationToken token = {}) = 0;
    virtual net::awaitable<void> rollback(CancellationToken token = {}) = 0;
};

using AsyncDriverFactory = std::function<std::unique_ptr<IAsyncDatabaseDriver>(net::any_io_executor)>;

} // namespace qornix::db
