/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <string>
#include <utility>

#include "db/async_db.h"
#include "db/mock_async_driver.h"
#include "config.h"

class AsyncTableManager;

class AsyncDatabaseInterface : public std::enable_shared_from_this<AsyncDatabaseInterface> {
public:
    AsyncDatabaseInterface(boost::asio::any_io_executor executor,
                           qornix::db::AsyncDriverFactory factory,
                           qornix::db::AsyncPoolOptions options = {},
                           std::shared_ptr<qornix::db::AsyncDbMetrics> metrics = std::make_shared<qornix::db::AsyncDbMetrics>());

    static std::shared_ptr<AsyncDatabaseInterface> createMock(
        boost::asio::any_io_executor executor,
        qornix::db::AsyncPoolOptions options = {},
        std::chrono::milliseconds latency = std::chrono::milliseconds{1});

    static qornix::db::AsyncPoolOptions poolOptionsFromConfig(const Config& config = Config::getInstance());

    AsyncTableManager table(const std::string& table_name);

    boost::asio::awaitable<void> warmup(qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::AsyncDbConnection> acquire(qornix::db::CancellationToken token = {});

    boost::asio::awaitable<qornix::db::QueryResult> query(
        std::string sql,
        qornix::db::QueryParams params = {},
        qornix::db::QueryOptions options = {},
        qornix::db::CancellationToken token = {});

    boost::asio::awaitable<qornix::db::QueryResult> queryOne(
        std::string sql,
        qornix::db::QueryParams params = {},
        qornix::db::QueryOptions options = {},
        qornix::db::CancellationToken token = {});

    boost::asio::awaitable<qornix::db::QueryResult> execute(
        std::string sql,
        qornix::db::QueryParams params = {},
        qornix::db::QueryOptions options = {},
        qornix::db::CancellationToken token = {});

    boost::asio::awaitable<qornix::db::AsyncTransaction> beginTransaction(qornix::db::CancellationToken token = {});
    boost::asio::awaitable<void> close();

    qornix::db::AsyncDbMetricsSnapshot metricsSnapshot() const;
    std::shared_ptr<qornix::db::AsyncDatabase> database() const noexcept { return database_; }

    DatabaseConfig getDatabaseConfig() const { return db_config_; }
    void setDatabaseConfig(DatabaseConfig config) { db_config_ = std::move(config); }

private:
    std::shared_ptr<qornix::db::AsyncDatabase> database_;
    DatabaseConfig db_config_{};
};
