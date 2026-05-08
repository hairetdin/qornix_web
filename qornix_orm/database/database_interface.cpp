/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "database_interface.h"

#include <iostream>
#include <stdexcept>
#include <utility>

#if QORNIX_ENABLE_POSTGRES
#include "drivers/PostgreSQL/PostgreSQLDriver.h"
#endif
#if QORNIX_ENABLE_SQLITE
#include "drivers/SQLite/SQLiteDriver.h"
#endif
#if QORNIX_ENABLE_MYSQL
#include "drivers/MySQL/MySQLDriver.h"
#endif

DatabaseInterface::DatabaseInterface(std::shared_ptr<IDatabase> db)
    : db_(std::move(db)), db_config_() {
}

TableManager DatabaseInterface::table(const std::string& tableName) {
    this -> reconnect();
    return ::table(this->db_, tableName);
}

void DatabaseInterface::connect() const {
    if (db_) {
        db_->connect(connectionString_);
    } else {
        throw std::runtime_error("Database driver is not set");
    }
}

void DatabaseInterface::reconnect() const {
    if (!db_) {
        throw std::runtime_error("Database driver is not set");
    }
    if (db_ && !connectionString_.empty()) {
        // Only reconnect if not currently connected
        if (!db_->isConnected()) {
            db_->connect(connectionString_);
        }
        // If already connected, do nothing
    } else {
        throw std::runtime_error("No stored connection string available for reconnection");
    }
}

void DatabaseInterface::disconnect() {
    if (db_ && db_->isConnected()) {
        db_->disconnect();
    }
}

bool DatabaseInterface::isConnected() const {
    return db_ && db_->isConnected();
}

std::vector<std::string> DatabaseInterface::getTableNames() {
    reconnect();
    return db_->getTableNames();
}

std::vector<std::string> DatabaseInterface::getColumnNames(const std::string& tableName) {
    reconnect();
    return db_->getColumnNames(tableName);
}

std::vector<std::string> DatabaseInterface::getTableFields(const std::string& tableName) {
    return getColumnNames(tableName);
}

DatabaseResponse DatabaseInterface::exec(const std::string& query, const std::vector<std::string>& params) {
    reconnect();
    return db_->exec(query, params);
}

std::string DatabaseInterface::executeQuery(const std::string &query, const std::string &dbSchema) {
    reconnect();
    try {
        return db_->executeQuery(query, dbSchema);
    } catch (const std::exception &e) {
        // Log the error or rethrow with more context
        throw std::runtime_error("Database query failed: " + std::string(e.what()) +
                                 " Query: " + query);
    }
}

std::string DatabaseInterface::executeQueryWithHeaders(const std::string &query) {
    reconnect();
    return db_->executeQueryWithHeaders(query);
}

std::string DatabaseInterface::executeQueryWithParams(const std::string & query, const std::vector<std::string> & params) {
    reconnect();
    return db_->executeQueryWithParams(query, params);
}


int DatabaseInterface::executeNonQuery(const std::string &query) {
    reconnect();
    return db_->executeNonQuery(query);
}

int DatabaseInterface::executeUpdate(const std::string &query) {
    reconnect();
    return db_->executeUpdate(query);
}

void DatabaseInterface::setDatabaseConfig(const DatabaseConfig &config) {
    // Create copy configuration for modification
    DatabaseConfig modified_config = config;

    // If connection string not specified, Create it from parameters
    if (config.connectionString.empty()) {
        modified_config.connectionString = createConnectionString(config);
    }

    // Save modified configuration
    db_config_ = modified_config;

    // Also Save connection string in field for internal use
    connectionString_ = modified_config.connectionString;
}

void DatabaseInterface::setConfigFromConnectionString(const std::string &connectionString,
                                                      const std::string &driverType) {
    auto &config = Config::getInstance();
    config.loadFromConnectionString(connectionString, driverType);

    // Get parsed configuration database
    DatabaseConfig parsedConfig = config.getDatabaseConfig();

    // Set Type driver, because as it not is contained in string connection
    parsedConfig.driver = driverType; // or Determine from connection string

    // Save parsed configuration in db_config_
    db_config_ = parsedConfig;

    // Save connection string in connectionString_
    connectionString_ = connectionString;
}

DatabaseConfig DatabaseInterface::getDatabaseConfig() const {
    return db_config_;
}

std::shared_ptr<IDatabase> DatabaseInterface::getDriver(const std::string &driverType) {
    if (driverType == "postgresql") {
#if QORNIX_ENABLE_POSTGRES
        return std::make_shared<PostgreSQLDriver>();
#else
        throw std::runtime_error("PostgreSQL driver is not built. Enable QORNIX_ENABLE_POSTGRES in CMake.");
#endif
    }
    if (driverType == "mysql") {
#if QORNIX_ENABLE_MYSQL
        return std::make_shared<MySQLDriver>();
#else
        throw std::runtime_error("MySQL driver is not built. Enable QORNIX_ENABLE_MYSQL in CMake.");
#endif
    }
    if (driverType == "sqlite" || driverType == "sqlite3") {
#if QORNIX_ENABLE_SQLITE
        return std::make_shared<SQLiteDriver>();
#else
        throw std::runtime_error("SQLite driver is not built. Enable QORNIX_ENABLE_SQLITE in CMake.");
#endif
    }

    throw std::runtime_error("Unsupported database driver: " + driverType);
}

std::unique_ptr<DatabaseInterface> DatabaseInterface::init(const std::string &configPath) {
    // Initialize from file configuration
    try {
        // Load configuration
        auto &config = Config::getInstance();
        config.reset();

        // Attempt load from file with specified path
        try {
            config.loadFromFile(configPath);
        } catch (const std::exception &e) {
            std::cout << "Config file not found at " << configPath << ", loading from environment variables" <<
                    std::endl;
            config.loadFromEnvironment();
        }

        // Get configuration database
        const DatabaseConfig dbConfig = config.getDatabaseConfig();

        // Initialize database with configuration
        return init(dbConfig);
    } catch (const std::exception &e) {
        throw std::runtime_error("Failed to init database: " + std::string(e.what()));
    }
}

std::unique_ptr<DatabaseInterface> DatabaseInterface::init(const DatabaseConfig &dbConfig) {
    try {
        // Create driver
        auto driver = DatabaseInterface::getDriver(dbConfig.driver);

        // Create interface database
        auto dbInterface = std::make_unique<DatabaseInterface>(driver);

        // Set configuration
        dbInterface->setDatabaseConfig(dbConfig);

        // Connection to database data
        dbInterface->connect();

        std::cout << "Successfully connected to database: " << dbConfig.dbname << std::endl;
        return dbInterface;
    } catch (const std::exception &e) {
        throw std::runtime_error("Failed to init database with config: " + std::string(e.what()));
    }
}


std::unique_ptr<DatabaseInterface> DatabaseInterface::init(
    const std::string &connectionString, const std::string &driverType) {
    try {
        auto &config = Config::getInstance();
        config.loadFromConnectionString(connectionString, driverType);
        auto dbConfig = config.getDatabaseConfig();

        // std::cout << "Database Config:" << std::endl;
        // std::cout << "Host: " << dbConfig.host << std::endl;
        // std::cout << "Port: " << dbConfig.port << std::endl;
        // std::cout << "DB Name: " << dbConfig.dbname << std::endl;
        // std::cout << "User: " << dbConfig.user << std::endl;
        // std::cout << "Driver: " << dbConfig.driver << std::endl;
        // std::cout << "Connection String: " << dbConfig.connectionString << std::endl;

        return init(dbConfig);
    } catch (const std::exception &e) {
        throw std::runtime_error("Failed to init database with connection string: " + std::string(e.what()));
    }
}
