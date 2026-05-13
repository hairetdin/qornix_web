/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "async_database_interface.h"
#include "async_table_manager.h"

AsyncDatabaseInterface::AsyncDatabaseInterface(
    boost::asio::any_io_executor executor,
    qornix::db::AsyncDriverFactory factory,
    qornix::db::AsyncPoolOptions options,
    std::shared_ptr<qornix::db::AsyncDbMetrics> metrics)
    : database_(std::make_shared<qornix::db::AsyncDatabase>(
          std::move(executor), std::move(factory), std::move(options), std::move(metrics))) {}

std::shared_ptr<AsyncDatabaseInterface> AsyncDatabaseInterface::createMock(
    boost::asio::any_io_executor executor,
    qornix::db::AsyncPoolOptions options,
    std::chrono::milliseconds latency) {
    DatabaseConfig config{};
    config.port = 0;
    config.driver = "mock_async";
    auto iface = std::make_shared<AsyncDatabaseInterface>(
        executor,
        qornix::db::make_mock_async_driver_factory(latency),
        options);
    iface->setDatabaseConfig(std::move(config));
    return iface;
}

AsyncTableManager AsyncDatabaseInterface::table(const std::string& table_name) {
    return AsyncTableManager{shared_from_this(), table_name};
}

boost::asio::awaitable<void> AsyncDatabaseInterface::warmup(qornix::db::CancellationToken token) {
    co_await database_->warmup(std::move(token));
}

boost::asio::awaitable<qornix::db::AsyncDbConnection> AsyncDatabaseInterface::acquire(qornix::db::CancellationToken token) {
    auto connection = co_await database_->acquire(std::move(token));
    co_return std::move(connection);
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncDatabaseInterface::query(
    std::string sql,
    qornix::db::QueryParams params,
    qornix::db::QueryOptions options,
    qornix::db::CancellationToken token) {
    auto result = co_await database_->query(std::move(sql), std::move(params), std::move(options), std::move(token));
    co_return result;
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncDatabaseInterface::queryOne(
    std::string sql,
    qornix::db::QueryParams params,
    qornix::db::QueryOptions options,
    qornix::db::CancellationToken token) {
    auto result = co_await database_->query_one(std::move(sql), std::move(params), std::move(options), std::move(token));
    co_return result;
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncDatabaseInterface::execute(
    std::string sql,
    qornix::db::QueryParams params,
    qornix::db::QueryOptions options,
    qornix::db::CancellationToken token) {
    auto result = co_await database_->execute(std::move(sql), std::move(params), std::move(options), std::move(token));
    co_return result;
}

boost::asio::awaitable<qornix::db::AsyncTransaction> AsyncDatabaseInterface::beginTransaction(qornix::db::CancellationToken token) {
    auto transaction = co_await database_->begin_transaction(std::move(token));
    co_return std::move(transaction);
}

boost::asio::awaitable<void> AsyncDatabaseInterface::close() {
    co_await database_->close();
}

qornix::db::AsyncDbMetricsSnapshot AsyncDatabaseInterface::metricsSnapshot() const {
    return database_->metrics_snapshot();
}
