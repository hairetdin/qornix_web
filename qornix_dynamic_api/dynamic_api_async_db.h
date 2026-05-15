/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#ifndef QORNIX_ENABLE_ASYNC_DB
#define QORNIX_ENABLE_ASYNC_DB 0
#endif

#if QORNIX_ENABLE_ASYNC_DB

#include "dynamic_api_config.h"
#include "dynamic_schema_allowlist.h"

#include "async_database_interface.h"
#include "database_interface.h"
#include "db/async_db_driver.h"
#include "db/sync_offloaded_async_driver.h"
#include "schema_document.h"

#if QORNIX_ENABLE_ASYNC_POSTGRES
#include "db/postgres_async_driver.h"
#endif

#if QORNIX_ENABLE_ASYNC_MYSQL
#include "db/mysql_async_driver.h"
#endif

#include <boost/asio.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace qornix_dynamic_api {

struct DynamicAsyncDatabaseOptions {
    qornix::db::AsyncPoolOptions pool;
    std::chrono::milliseconds connectTimeout{std::chrono::milliseconds{5000}};
    std::size_t offloadedWorkerThreads{1};
};

inline std::string normalizedDynamicDbDriver(std::string driver) {
    std::transform(driver.begin(), driver.end(), driver.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (driver == "postgresql") {
        return "postgres";
    }
    if (driver == "sqlite3") {
        return "sqlite";
    }
    return driver;
}

inline bool dynamicAsyncDbUsesOffloadedSyncDriver(const DatabaseConfig& config) {
    const auto driver = normalizedDynamicDbDriver(config.driver);
    return driver == "sqlite" || driver == "sync_offloaded";
}

inline bool dynamicAsyncDbSupportsPreparedQueries(const DatabaseConfig& config) {
    return !dynamicAsyncDbUsesOffloadedSyncDriver(config);
}

inline std::string dynamicAsyncConnectionString(const DatabaseConfig& config) {
    if (!config.connectionString.empty()) {
        return config.connectionString;
    }
    return createConnectionString(config);
}

inline std::shared_ptr<AsyncDatabaseInterface> makeDynamicAsyncDatabaseInterface(
    boost::asio::any_io_executor executor,
    DatabaseConfig config,
    DynamicAsyncDatabaseOptions options = {}) {
    const auto driver = normalizedDynamicDbDriver(config.driver);
    auto pool = std::move(options.pool);

    qornix::db::AsyncDriverFactory factory;
    if (driver == "postgres") {
#if QORNIX_ENABLE_ASYNC_POSTGRES
        pool.driver_name = pool.driver_name.empty() ? "postgres_async" : pool.driver_name;
        qornix::db::PostgresAsyncOptions pg;
        pg.connection_info = dynamicAsyncConnectionString(config);
        pg.connect_timeout = options.connectTimeout;
        pg.default_query_timeout = pool.query_timeout;
        factory = qornix::db::make_postgres_async_driver_factory(std::move(pg));
#else
        throw std::runtime_error("Dynamic API async PostgreSQL requires QORNIX_ENABLE_ASYNC_POSTGRES=ON");
#endif
    } else if (driver == "mysql") {
#if QORNIX_ENABLE_ASYNC_MYSQL
        pool.driver_name = pool.driver_name.empty() ? "mysql_async" : pool.driver_name;
        qornix::db::MySqlAsyncOptions mysql;
        mysql.connection_info = config.connectionString;
        if (!config.host.empty()) {
            mysql.host = config.host;
        }
        if (config.port > 0) {
            mysql.port = std::to_string(config.port);
        }
        mysql.username = config.user;
        mysql.password = config.password;
        mysql.database = config.dbname;
        mysql.connect_timeout = options.connectTimeout;
        mysql.default_query_timeout = pool.query_timeout;
        factory = qornix::db::make_mysql_async_driver_factory(std::move(mysql));
#else
        throw std::runtime_error("Dynamic API async MySQL requires QORNIX_ENABLE_ASYNC_MYSQL=ON");
#endif
    } else if (driver == "sqlite") {
        pool.driver_name = pool.driver_name.empty() ? "sqlite_sync_offloaded" : pool.driver_name;
        const auto connectionString = dynamicAsyncConnectionString(config);
        const auto workerThreads = options.offloadedWorkerThreads;
        factory = [config, connectionString, workerThreads](boost::asio::any_io_executor driverExecutor)
            -> std::unique_ptr<qornix::db::IAsyncDatabaseDriver> {
            auto syncDriver = DatabaseInterface::getDriver(config.driver.empty() ? "sqlite" : config.driver);
            return std::make_unique<qornix::db::SyncOffloadedAsyncDriver>(
                std::move(driverExecutor),
                std::move(syncDriver),
                connectionString,
                workerThreads);
        };
    } else {
        throw std::runtime_error("Unsupported Dynamic API async DB driver: " + config.driver);
    }

    auto database = std::make_shared<AsyncDatabaseInterface>(
        std::move(executor),
        std::move(factory),
        std::move(pool));
    database->setDatabaseConfig(std::move(config));
    return database;
}

inline DynamicSchemaAllowlist loadDynamicAsyncAllowlist(const DynamicApiConfig& config) {
    if (!config.uploadedSchemaPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(config.uploadedSchemaPath, ec)) {
            auto parsed = SchemaDocument::loadFromFile(
                config.uploadedSchemaPath,
                config.schemaXsdPath,
                SchemaDocumentSourceType::UploadedXml);
            if (parsed.ok() && parsed.document) {
                return DynamicSchemaAllowlist::fromSchemaDocument(*parsed.document);
            }
        }
    }

    auto database = DatabaseInterface::init(config.databaseConfig);
    return DynamicSchemaAllowlist::fromDatabase(*database);
}

} // namespace qornix_dynamic_api

#endif // QORNIX_ENABLE_ASYNC_DB
