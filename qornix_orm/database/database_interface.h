/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include "table_manager.h"
#include "IDatabase.h"
#include <memory>
#include <string>

#include "config.h"

class DatabaseInterface {
public:
    explicit DatabaseInterface(std::shared_ptr<IDatabase> db);

    TableManager table(const std::string &tableName);

    // Methods for getting configuration
    DatabaseConfig getDatabaseConfig() const;

    // Method for setting configuration
    void setConfigFromConnectionString(const std::string &connectionString,
                                       const std::string &driverType = "postgresql");

    void setDatabaseConfig(const DatabaseConfig &config);

    // Direct access to IDatabase metodam (proxy)
    void reconnect() const;

    void connect() const;

    void disconnect();

    bool isConnected() const;

    // Metadata schema
    std::vector<std::string> getTableNames();
    std::vector<std::string> getColumnNames(const std::string& tableName);
    std::vector<std::string> getTableFields(const std::string& tableName);

    // Unified Method execution requests
    DatabaseResponse exec(const std::string& query, const std::vector<std::string>& params = {});

    std::string executeQuery(const std::string &query, const std::string &dbSchema = "public");

    std::string executeQueryWithHeaders(const std::string &query);

    std::string executeQueryWithParams(const std::string & sql, const std::vector<std::string> & vector);

    int executeNonQuery(const std::string &query);

    int executeUpdate(const std::string &query);

    // Create driver in depending from type database
    static std::shared_ptr<IDatabase> getDriver(const std::string &driverType);

    std::shared_ptr<IDatabase> getIDatabase() const { return db_; }
    std::shared_ptr<IDatabase> db_;

    // Initialize database on basis configuration
    static std::unique_ptr<DatabaseInterface> init(const std::string &configPath = "config.yaml");

    // Initialize with specific configuration
    static std::unique_ptr<DatabaseInterface> init(const DatabaseConfig &dbConfig);

    // Initialize through connection string
    static std::unique_ptr<DatabaseInterface> init(
        const std::string &connectionString, const std::string &driverType);

private:
    std::string connectionString_; // Store connection string for reconnection
    DatabaseConfig db_config_;
};
