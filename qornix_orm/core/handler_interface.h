/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>

#include "database_interface.h"
#include "app_struct.h"
#include "schema_interface.h"
#include "config_interface.h"
#include "entity_interface.h"
#include "model_interface.h"

class DBMigrator;

class QornixHandler {
private:
    std::string appName_;
    std::string schemaFile_;
    std::shared_ptr<DBMigrator> dbMigrator_;
    static void setupDatabaseConfiguration(const DatabaseConfig &dbConfig);

public:
    std::shared_ptr<ConfigInterface> config;
    std::shared_ptr<DatabaseInterface> db;
    std::shared_ptr<SchemaInterface> schema;
    std::shared_ptr<EntityInterface> entity;
    std::shared_ptr<ModelInterface> model;

    // The constructor initializes db and schema
    QornixHandler():
        config(std::make_shared<ConfigInterface>()),
        schema(std::make_shared<SchemaInterface>()),
        entity(std::make_shared<EntityInterface>()),
        model(std::make_shared<ModelInterface>())
    {}

    // Initialize application with database connection
    bool init(const std::string& configFilePath, const std::string& schemaFile = "");

    bool loadConfigAndConnetDb(const std::string &configPath="config.yaml");
    bool dbConnect(const std::string &configPath="config.yaml");
    bool dbConnect(const DatabaseConfig &dbConfig);
    bool dbConnect(const std::string &connectionString, const std::string &driverType);

    void syncEntityWithDatabase(std::shared_ptr<DatabaseInterface> db);

    // Process entities defined in DataStructureType
    static void processDataStructure();

    // Process views defined in Views/ViewType
    static void processViews();

    // Process stored procedures
    static void processStoredProcedures();

    // Process triggers
    static void processTriggers();

    // Process database functions
    static void processDatabaseFunctions();

    bool buildMigrations();

    static void printSchemaComparisonResult(const SchemaComparisonResult &result);
};
