/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <pugixml.hpp>
#include "database_interface.h"
#include "entity_base.h"
#include "handler_interface.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <functional>
#include <utility>

#include "schema_loader.h"
#include "entity_interface.h"

//  QornixHandler
bool QornixHandler::loadConfigAndConnetDb(const std::string &configPath) {
    // Load configuration, connect to the database, and pass the database to the schema
    try {
        // Load configuration from the specified file
        // config = std::make_shared<ConfigInterface>();  // initialization is in the constructor
        if (!config->loadFromFile(configPath)) {
            std::cerr << "Failed to load configuration from: " << configPath << std::endl;
            return false;
        }

        // Get the connection string through the new method
        std::string connectionString = config->getDbConnectionString();
        if (connectionString.empty()) {
            std::cerr << "Failed to generate connection string from config" << std::endl;
            return false;
        }

        // Get the driver type
        std::string driverType = config->get("db.driver", "postgresql");

        // std::cout  << "DEBUG: Connection string: " << connectionString << std::endl;
        // Create a database connection
        db = DatabaseInterface::init(connectionString, driverType);

        if (!db || !db->isConnected()) {
            std::cerr << "Failed to establish database connection" << std::endl;
            return false;
        }
        //  Pass db to the schema
        this -> schema -> setDb(db);

        std::cout << "Database connection established successfully using config: " << configPath << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Database connection error: " << e.what() << std::endl;
        return false;
    }
}

bool QornixHandler::dbConnect(const std::string &configPath) {
    try {
        db = DatabaseInterface::init(configPath);
        return db && db->isConnected();
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        return false;
    }
}

bool QornixHandler::dbConnect(const DatabaseConfig &dbConfig) {
    try {
        db = DatabaseInterface::init(dbConfig);
        return db && db->isConnected();
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        return false;
    }
}

bool QornixHandler::dbConnect(const std::string &connectionString, const std::string &driverType) {
    try {
        db = DatabaseInterface::init(connectionString, driverType);
        return db && db->isConnected();
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        return false;
    }
}

void QornixHandler::setupDatabaseConfiguration(const DatabaseConfig& dbConfig) {
    ConfigurationDefinition& configuration_ = SchemaLoader::configuration_;
    configuration_.databaseEngine = dbConfig.driver;
    // std::cout << "DEBUG: Database engine: " << dbConfig.driver << std::endl;

    configuration_.dbConnectionString = dbConfig.connectionString;
    // std::cout << "DEBUG: Database connection string: " << dbConfig.connectionString << std::endl;
}

bool QornixHandler::init(const std::string& configFilePath, const std::string& schemaFile) {
    /*
     * Use the schema generated from the database
     * SchemaLoader::saveSchemaFromDatabase(dbShared, "../schema/schema_generated.xml")
     * Or load the schema from the default file: SchemaLoader::loadSchemaFromFile(schemaFile)
     */
    try {
        std::cout << "Initializing QornixHandler..." << std::endl;

        std::string configFile = configFilePath.empty() ? "config.yaml" : configFilePath;

        // Connect to database
        std::cout << "Загружаем конфигурацию " << configFile << ", подключаемся к БД, передаем БД в схему" << std::endl;
        if (!loadConfigAndConnetDb(configFile)) {
            std::cerr << "Failed to initialize database connection from config: " << configFile << std::endl;
            return false;
        }

        // // Create a temporary handler for the connection
        // auto tempHandler = QornixHandler::dbConnect(configFile);
        // if (!tempHandler || !tempHandler->db) {
        //     std::cerr << "Failed to connect to database" << std::endl;
        //     return false;
        // }
        //
        // // Pass the connection to the current instance
        // this->db = tempHandler->db;

        // Connect to database
        // this -> db = DatabaseInterface::init(configFile);
        // if (!db->init(configFile)) {
        //     std::cerr << "Failed to connect to database" << std::endl;
        //     return false;
        // }
        //
        // if (!db->isConnected()) {
        //     std::cerr << "Database connection failed" << std::endl;
        //     return false;
        // }

        // this -> schema -> setDb(db);

        std::cout << "Database connection established successfully" << std::endl;
        // DatabaseConfig dbConfig = db->getDatabaseConfig();
        // setupDatabaseConfiguration(dbConfig);

        // If the schema is not specified, generate it from the database
        if (schemaFile.empty()) {
            std::cout << "No schema file specified, generating schema from database..." << std::endl;
            if (!SchemaLoader::saveSchemaFromDatabase(db, "schema_generated.xml")) {
                std::cerr << "Failed to generate schema from database" << std::endl;
                return false;
            }
            std::cout << "Schema generated from database successfully" << std::endl;
        } else {
            // Load application schema from specified file
            std::cout << "Loading application schema from: " << schemaFile << std::endl;
            if (!SchemaLoader::loadSchemaFromFile(schemaFile)) {
                std::cerr << "Failed to load application schema" << std::endl;
                return false;
            }
            std::cout << "Application schema loaded successfully" << std::endl;

            // Load schema from database
            std::cout << "Loading schema from database..." << std::endl;
            if (!SchemaLoader::loadSchemaFromDB(db)) {
                std::cerr << "Failed to load schema from database" << std::endl;
                // Continue with just the XML schema
            } else {
                std::cout << "Schema loaded from database successfully" << std::endl;
            }
        }

        SchemaComparisonResult schemaComparisonResult_ = SchemaLoader::compareSchemaEntities();
//        schemaComparisonResult_ = SchemaLoader::compareSchemaViews(schemaComparisonResult_);
        printSchemaComparisonResult(schemaComparisonResult_);

        // Load schema metadata: is it needed?
//        std::cout << "Loading schema metadata..." << std::endl;
//        SchemaMetadata metadata = SchemaLoader::loadSchemaMetadata(schemaFile);
//
//        std::cout << "Schema version: " << metadata.version << std::endl;
//        std::cout << "Schema created at: " << metadata.createdAt << std::endl;

        if (!SchemaLoader::applySchemaChangesToDB(db)) {
            std::cerr << "Failed to apply schema changes to database" << std::endl;
            // Handle error appropriately
        }

        // Initialize entity manager with loaded schema
        std::cout << "Initializing entity manager with loaded schema" << std::endl;
        EntityInterface::init(db);
        ModelInterface::setDb(db);
        EntityInterface::init(db);
        std::cout << "Entity manager initialized successfully" << std::endl;

        // Initialize dbMigrator
        // this->dbMigrator_ = std::make_shared<DBMigrator>(dbShared, "./db_migrations");
        // SchemaLoader::setDBMigrator(this->dbMigrator_);

        std::cout << "QornixHandler initialization completed successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Initialization error: " << e.what() << std::endl;
        return false;
    }
}

void QornixHandler::printSchemaComparisonResult(const SchemaComparisonResult& result) {
    std::cout << "=== Schema Comparison Result ===" << std::endl;
    std::cout << "Schemas match: " << (result.schemasMatch ? "Yes" : "No") << std::endl;

    // Output missing entities
    if (!result.missingEntities.empty()) {
        std::cout << "Missing entities in database:" << std::endl;
        for (const auto& entity : result.missingEntities) {
            std::cout << "  - " << entity << std::endl;
        }
    }

    // Output extra entities
    if (!result.extraEntities.empty()) {
        std::cout << "Extra entities in database:" << std::endl;
        for (const auto& entity : result.extraEntities) {
            std::cout << "  - " << entity << std::endl;
        }
    }

    // Output entity differences
    if (!result.entityDifferences.empty()) {
        std::cout << "Entity differences:" << std::endl;
        for (const auto& pair : result.entityDifferences) {
            const std::string& entityName = pair.first;
            const EntityDifferences& differences = pair.second;

            std::cout << "  " << entityName << ":" << std::endl;

            if (!differences.missingFields.empty()) {
                std::cout << "    Missing fields:" << std::endl;
                for (const auto& field : differences.missingFields) {
                    std::cout << "      - " << field << std::endl;
                }
            }

            if (!differences.extraFields.empty()) {
                std::cout << "    Extra fields:" << std::endl;
                for (const auto& field : differences.extraFields) {
                    std::cout << "      - " << field << std::endl;
                }
            }

            if (!differences.fieldMismatches.empty()) {
                std::cout << "    Field mismatches:" << std::endl;
                for (const auto& field : differences.fieldMismatches) {
                    std::cout << "      - " << field << std::endl;
                }
            }

            if (!differences.foreignKeyDifferences.empty()) {
                std::cout << "    Foreign key differences:" << std::endl;
                for (const auto& fk : differences.foreignKeyDifferences) {
                    std::cout << "      - " << fk << std::endl;
                }
            }

            if (!differences.indexDifferences.empty()) {
                std::cout << "    Index differences:" << std::endl;
                for (const auto& index : differences.indexDifferences) {
                    std::cout << "      - " << index << std::endl;
                }
            }

            if (!differences.constraintDifferences.empty()) {
                std::cout << "    Constraint differences:" << std::endl;
                for (const auto& constraint : differences.constraintDifferences) {
                    std::cout << "      - " << constraint << std::endl;
                }
            }
        }
    }

    // Output missing views
    if (!result.missingViews.empty()) {
        std::cout << "Missing views in database:" << std::endl;
        for (const auto& view : result.missingViews) {
            std::cout << "  - " << view << std::endl;
        }
    }

    // Output extra views
    if (!result.extraViews.empty()) {
        std::cout << "Extra views in database:" << std::endl;
        for (const auto& view : result.extraViews) {
            std::cout << "  - " << view << std::endl;
        }
    }

    // Output view differences
    if (!result.viewDifferences.empty()) {
        std::cout << "View differences:" << std::endl;
        for (const auto& pair : result.viewDifferences) {
            const std::string& viewName = pair.first;
            const ViewDifferences& differences = pair.second;

            std::cout << "  " << viewName << ":" << std::endl;
            for (const auto& mismatch : differences.propertyMismatches) {
                std::cout << "    - " << mismatch << std::endl;
            }
        }
    }

    std::cout << "================================" << std::endl;
}

void QornixHandler::processDataStructure() {
    // Based on DataStructureType from schema:
    // 1. Create tables for each Entity
    // 2. Add fields with appropriate types
    // 3. Create foreign key relationships
    // 4. Add indexes and constraints

    std::cout << "Processing entity definitions..." << std::endl;
    SchemaLoader::processDataStructure();
    // Implementation would parse Entity elements and create corresponding tables
}

void QornixHandler::processViews() {
    // Process ViewType and MaterializedViewType elements
    std::cout << "Creating views..." << std::endl;
}

void QornixHandler::processStoredProcedures() {
    // Process StoredProcedure elements
    std::cout << "Creating stored procedures..." << std::endl;
}

void QornixHandler::processTriggers() {
    // Process Trigger elements
    std::cout << "Setting up triggers..." << std::endl;
}

void QornixHandler::processDatabaseFunctions() {
    // Process DatabaseFunction elements
    std::cout << "Creating database functions..." << std::endl;
}

void QornixHandler::syncEntityWithDatabase(std::shared_ptr<DatabaseInterface> db_interface) {
    db_interface = std::move(db_interface);
    SchemaLoader::loadSchemaFromDB(db_interface);
    // auto entities = SchemaLoader::getEntities();
    // for (auto &entity : entities) {
    //     std::cout << "DEBUG: Processing entity: " << entity.first << std::endl;
    // }
    SchemaLoader::syncEntitiesWithDatabase();
    // SchemaLoader::saveSchemaFromDatabase(db_interface, "../schema/schema_generated.xml");
}

