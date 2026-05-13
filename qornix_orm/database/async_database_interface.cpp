/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "async_database_interface.h"
#include "async_table_manager.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <string>


namespace {

std::string getFirstConfigValue(const Config& config, const std::initializer_list<const char*>& keys) {
    for (const char* key : keys) {
        const auto value = config.get(key);
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

std::size_t getSizeConfigValue(const Config& config,
                               const std::initializer_list<const char*>& keys,
                               std::size_t fallback) {
    const auto value = getFirstConfigValue(config, keys);
    if (value.empty()) {
        return fallback;
    }
    return static_cast<std::size_t>(std::stoull(value));
}

std::chrono::milliseconds getMsConfigValue(const Config& config,
                                           const std::initializer_list<const char*>& keys,
                                           std::chrono::milliseconds fallback) {
    const auto value = getFirstConfigValue(config, keys);
    if (value.empty()) {
        return fallback;
    }
    return std::chrono::milliseconds{std::stoll(value)};
}

bool getBoolConfigValue(const Config& config,
                        const std::initializer_list<const char*>& keys,
                        bool fallback) {
    auto value = getFirstConfigValue(config, keys);
    if (value.empty()) {
        return fallback;
    }
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

} // namespace

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


qornix::db::AsyncPoolOptions AsyncDatabaseInterface::poolOptionsFromConfig(const Config& config) {
    qornix::db::AsyncPoolOptions options;
    options.pool_name = getFirstConfigValue(config, {"db.pool.name", "database.pool.name"});
    if (options.pool_name.empty()) {
        options.pool_name = "default";
    }
    options.driver_name = getFirstConfigValue(config, {"db.driver", "database.driver"});
    options.min_connections = getSizeConfigValue(config, {"db.pool.min_connections", "database.pool.min_connections"}, options.min_connections);
    options.max_connections = getSizeConfigValue(config, {"db.pool.max_connections", "database.pool.max_connections"}, options.max_connections);
    options.max_waiters = getSizeConfigValue(config, {"db.pool.max_waiters", "database.pool.max_waiters"}, options.max_waiters);
    options.acquire_timeout = getMsConfigValue(config, {"db.pool.acquire_timeout_ms", "database.pool.acquire_timeout_ms"}, options.acquire_timeout);
    options.query_timeout = getMsConfigValue(config, {"db.pool.query_timeout_ms", "database.pool.query_timeout_ms"}, options.query_timeout);
    options.idle_timeout = getMsConfigValue(config, {"db.pool.idle_timeout_ms", "database.pool.idle_timeout_ms"}, options.idle_timeout);
    options.max_lifetime = getMsConfigValue(config, {"db.pool.max_lifetime_ms", "database.pool.max_lifetime_ms"}, options.max_lifetime);
    options.health_check_interval = getMsConfigValue(config, {"db.pool.health_check_interval_ms", "database.pool.health_check_interval_ms"}, options.health_check_interval);
    options.shutdown_timeout = getMsConfigValue(config, {"db.pool.shutdown_timeout_ms", "database.pool.shutdown_timeout_ms"}, options.shutdown_timeout);
    options.validate_idle_on_acquire = getBoolConfigValue(config, {"db.pool.validate_idle_on_acquire", "database.pool.validate_idle_on_acquire"}, options.validate_idle_on_acquire);
    return options;
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
