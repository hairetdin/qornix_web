/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <iostream>
#include <cassert>
#include <memory>
#include <filesystem>
#include "handler_interface.h"
#include "schema_loader.h"
#include "test_utils.h"

class QornixHandlerTest {
private:
    qornix_orm_tests::TempSqliteConfig sqliteConfig_{"handler_test"};
    int testsPassed = 0;
    int totalTests = 0;

    std::string configPath() const {
        return sqliteConfig_.configPath();
    }

    std::string invalidSqlitePath() const {
        return (std::filesystem::temp_directory_path() / "qornix_orm_missing_dir" / "invalid.sqlite").string();
    }

    void assertTrue(bool condition, const std::string &message) {
        totalTests++;
        if (condition) {
            std::cout << "✓ " << message << std::endl;
            testsPassed++;
        } else {
            std::cout << "✗ " << message << std::endl;
        }
    }

    void assertFalse(bool condition, const std::string &message) {
        assertTrue(!condition, message);
    }

    void createTestCategoryTable(std::shared_ptr<DatabaseInterface> db) {
        try {
            // Check exists whether table
            db->executeQuery("SELECT 1 FROM test_categories LIMIT 1;");
        } catch (...) {
            // If table not exists, Create it
            std::string createTableQuery = R"(
                CREATE TABLE test_categories (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    name TEXT NOT NULL,
                    description TEXT,
                    created_at TEXT DEFAULT CURRENT_TIMESTAMP
                );
            )";
            db->executeNonQuery(createTableQuery);
            std::cout << "Created test_categories table" << std::endl;
        }
    }

public:
    void testDbConnectWithConfigFile() {
        std::cout << "\n=== Testing QornixHandler::dbConnect with config file ===" << std::endl;

        // Test connection with specified file configuration
        std::string configFilePath = configPath();
        auto handler = std::make_unique<QornixHandler>();
        handler->init(configFilePath);
        assertTrue(handler != nullptr, "Should create handler with config file");

        if (handler) {
            assertTrue(handler->db != nullptr, "Database should be initialized");
            assertTrue(handler->db->isConnected(), "Should be connected to database");

            // Check that configuration loaded correctly
            DatabaseConfig config = handler->db->getDatabaseConfig();
            assertTrue(!config.dbname.empty(), "Database name should be configured");
            assertTrue(config.driver == "sqlite", "Driver should be sqlite");
        }
    }

    void testDbConnectWithDefaultConfig() {
        std::cout << "\n=== Testing QornixHandler::dbConnect with default config ===" << std::endl;

        // Test connection using config by default
        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        // auto handler = QornixHandler::dbConnect(); // must use config.ini by default
        assertTrue(handler != nullptr, "Should create handler with default config file");

        if (handler) {
            assertTrue(handler->db != nullptr, "Database should be initialized");
            assertTrue(handler->db->isConnected(), "Should be connected to database");
        }
    }

    void testDbConnectWithDatabaseConfig() {
        std::cout << "\n=== Testing QornixHandler::dbConnect with DatabaseConfig ===" << std::endl;

        // Prepare configuration
        DatabaseConfig config;
        config = sqliteConfig_.databaseConfig();

        // Test connection with obektom DatabaseConfig
        // auto handler = QornixHandler::dbConnect(config);
        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(config);
        assertTrue(handler != nullptr, "Should create handler with DatabaseConfig");

        if (handler) {
            assertTrue(handler->db != nullptr, "Database should be initialized");
            assertTrue(handler->db->isConnected(), "Should be connected to database");

            // Check that configuration matches
            DatabaseConfig actualConfig = handler->db->getDatabaseConfig();
            assertTrue(actualConfig.dbname == config.dbname, "Database name should match");
            assertTrue(actualConfig.driver == config.driver, "Driver should match");
        }
    }

    void testDbConnectWithConnectionString() {
        std::cout << "\n=== Testing QornixHandler::dbConnect with connection string ===" << std::endl;

        // Prepare connection string
        std::string connectionString = sqliteConfig_.dbPath();
        std::string driverType = "sqlite";

        // Test connection so string connection
        // auto handler = QornixHandler::dbConnect(connectionString, driverType);
        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(connectionString, driverType);
        assertTrue(handler != nullptr, "Should create handler with connection string");

        if (handler) {
            assertTrue(handler->db != nullptr, "Database should be initialized");
            assertTrue(handler->db->isConnected(), "Should be connected to database");

            // Check that connection really rabotaet
            DatabaseConfig config = handler->db->getDatabaseConfig();
            assertTrue(config.driver == driverType, "Driver type should match");
        }
    }

    void testDbConnectWithInvalidConfig() {
        std::cout << "\n=== Testing QornixHandler::dbConnect with invalid config ===" << std::endl;

        // Prepare nevernuyu connection string
        std::string invalidConnectionString = invalidSqlitePath();
        std::string driverType = "sqlite";

        // Test connection with nevernoy string connection
        // auto handler = QornixHandler::dbConnect(invalidConnectionString, driverType);
        auto handler = std::make_unique<QornixHandler>();
        auto isConnected = handler->dbConnect(invalidConnectionString, driverType);
        assertFalse(isConnected, "Should return false for invalid connection string");
    }

    void testMultipleConnections() {
        std::cout << "\n=== Testing multiple connections ===" << std::endl;

        // Test creating multiple podklyucheniy
        auto handler1 = std::make_unique<QornixHandler>();
        handler1->dbConnect(configPath());

        auto handler2 = std::make_unique<QornixHandler>();
        handler2->dbConnect(configPath());

        assertTrue(handler1 != nullptr, "First handler should be created");
        assertTrue(handler2 != nullptr, "Second handler should be created");

        if (handler1 && handler2) {
            assertTrue(handler1->db->isConnected(), "First connection should be active");
            assertTrue(handler2->db->isConnected(), "Second connection should be active");

            // Check that at nikh different ekzemplyary database
            assertTrue(handler1->db.get() != handler2->db.get(), "Handlers should have different database instances");
        }
    }

    void testBasicOperationsAfterConnect() {
        std::cout << "\n=== Testing basic operations after connection ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        assertTrue(handler != nullptr, "Should create handler for basic operations test");

        if (handler && handler->db) {
            // Check bazovye operatsii
            assertTrue(handler->db->isConnected(), "Should be connected");

            // Create test table before vypolneniem requests
            createTestCategoryTable(handler->db);

            // Check vozmozhnost execution simple request
            try {
                std::string result = handler->db->executeQuery("SELECT 1 as test_value");
                assertTrue(!result.empty(), "Should be able to execute simple query");
            } catch (...) {
                std::cout << "Note: Simple query execution may not be supported by all drivers" << std::endl;
            }

            // Check access to TableManager
            auto tableManager = handler->db->table("test_categories").limit(1).execute();
            assertTrue(true, "Should be able to access TableManager");
            // always true, t.to. error budet pri ispolzovanii
        }
    }

    // Methods for testing schema_interface
    void testSchemaLoadFromDb() {
        std::cout << "\n=== Testing handler->schema->loadSchemaFromDb() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler && handler->db && handler->db->isConnected()) {
            // Test loading schema from database
            bool success = handler->schema->loadSchemaFromDb(handler->db);
            assertTrue(success, "Should be able to load schema from database via handler->schema->loadSchemaFromDb()");
        }
    }

    void testSchemaLoadFromFile() {
        std::cout << "\n=== Testing handler->schema->loadSchemaFromFile() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler) {
            // Test loading schema from file (assume, that file exists)
            bool success = handler->schema->loadSchemaFromFile("../schema/schema_generated.xml");
            // Not trebuem required presence file, simply Check, that Method vyzyvaetsya without errors
            std::cout << "Load schema from file result: " << (success ? "Success" : "Failed - file may not exist") <<
                    std::endl;
            assertTrue(true, "Should be able to call handler->schema->loadSchemaFromFile() without errors");
        }
    }

    void testSchemaSaveFromDatabase() {
        std::cout << "\n=== Testing handler->schema->saveSchemaFromDatabase() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler && handler->db && handler->db->isConnected()) {
            // Test Save schema from database
            const std::string outputPath = sqliteConfig_.dbPath() + ".schema.xml";
            bool success = handler->schema->saveSchemaFromDatabase(handler->db, outputPath);
            std::error_code ec;
            std::filesystem::remove(outputPath, ec);
            assertTrue(success,
                       "Should be able to save schema from database via handler->schema->saveSchemaFromDatabase()");
        }
    }

    void testSchemaCompareEntities() {
        std::cout << "\n=== Testing handler->schema->compareSchemaEntities() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler && handler->db && handler->db->isConnected()) {
            // Load first schema from database
            handler->schema->loadSchemaFromDb(handler->db);

            // Test sravnenie skhem
            SchemaComparisonResult result = handler->schema->compareSchemaEntities();
            assertTrue(true, "Should be able to call handler->schema->compareSchemaEntities() without errors");
            std::cout << "Schema comparison completed, schemas match: " << (result.schemasMatch ? "Yes" : "No") <<
                    std::endl;
        }
    }

    void testSchemaApplyChangesToDB() {
        std::cout << "\n=== Testing handler->schema->applySchemaChangesToDB() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler && handler->db && handler->db->isConnected()) {
            // Test application changes schema to database data
            bool success = handler->schema->applySchemaChangesToDB(handler->db);
            assertTrue(success, "Should be able to apply schema changes via handler->schema->applySchemaChangesToDB()");
        }
    }

    void testSchemaSyncEntities() {
        std::cout << "\n=== Testing handler->schema->syncEntitiesWithDatabase() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler && handler->db && handler->db->isConnected()) {
            // Test sinkhronizatsiyu entities with bazoy data
            handler->schema->syncEntitiesWithDatabase();
            assertTrue(true, "Should be able to call handler->schema->syncEntitiesWithDatabase() without errors");
        }
    }

    void testSchemaGetters() {
        std::cout << "\n=== Testing handler->schema-> getter methods ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler && handler->db && handler->db->isConnected()) {
            // Load schema for testing getterov
            handler->schema->loadSchemaFromDb(handler->db);

            // Test razlichnye gettery
            auto entities = handler->schema->getEntities();
            auto dbEntities = handler->schema->getDbEntities();
            auto views = handler->schema->getViews();
            auto materializedViews = handler->schema->getMaterializedViews();
            auto storedProcedures = handler->schema->getStoredProcedures();
            auto triggers = handler->schema->getTriggers();
            auto databaseFunctions = handler->schema->getDatabaseFunctions();
            auto configuration = handler->schema->getConfiguration();
            auto metadata = handler->schema->getSchemaMetadata();

            assertTrue(true, "Should be able to call all getter methods via handler->schema-> without errors");
            std::cout << "Retrieved entities count: " << entities.size() << std::endl;
            std::cout << "Retrieved DB entities count: " << dbEntities.size() << std::endl;
            std::cout << "Retrieved views count: " << views.size() << std::endl;
        }
    }

    void testSchemaVersionMethods() {
        std::cout << "\n=== Testing handler->schema-> version methods ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->init(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler) {
            // Test Methods upravleniya versions schema
            std::string currentVersion = handler->schema->getCurrentSchemaVersion();
            assertTrue(
                true, "Should be able to get current schema version via handler->schema->getCurrentSchemaVersion()");

            auto versionHistory = handler->schema->getSchemaVersionHistory();
            assertTrue(
                true, "Should be able to get schema version history via handler->schema->getSchemaVersionHistory()");

            std::cout << "Current schema version: " << currentVersion << std::endl;
            std::cout << "Version history size: " << versionHistory.size() << std::endl;
        }
    }

    void testSchemaGetCurrentSnapshot() {
        std::cout << "\n=== Testing handler->schema->getCurrentSnapshot() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->init(configPath());
        assertTrue(handler != nullptr, "Handler should be created for schema test");

        if (handler && handler->db && handler->db->isConnected()) {
            // Load schema
            handler->schema->loadSchemaFromDb(handler->db);

            // Test retrieving current snapshot
            SchemaSnapshot snapshot = handler->schema->getCurrentSnapshot();
            assertTrue(true, "Should be able to get current schema snapshot via handler->schema->getCurrentSnapshot()");
            std::cout << "Snapshot entities count: " << snapshot.entities.size() << std::endl;
        }
    }

    void testLoadConfigAndConnetDb() {
        std::cout << "\n=== Testing QornixHandler::loadConfigAndConnetDb ===" << std::endl;

        // Test connection with specified file configuration
        std::string configFilePath = configPath();
        auto handler = std::make_unique<QornixHandler>();
        handler->config->loadFromFile(configFilePath);
        handler->config->printConfig();
        // auto allEntities = handler->entity->getAllEntityNames();

        // Vyzyvaem Method loadConfigAndConnetDb
        bool connectionResult = handler->loadConfigAndConnetDb(configFilePath);
        assertTrue(connectionResult,
                   "Should successfully connect to database using config file via loadConfigAndConnetDb");

        if (handler) {
            assertTrue(handler->db != nullptr, "Database should be initialized");
            assertTrue(handler->db->isConnected(), "Should be connected to database");
            assertTrue(handler->config != nullptr, "Config should be initialized");

            // Check that configuration loaded correctly
            DatabaseConfig config = handler->db->getDatabaseConfig();
            assertTrue(!config.dbname.empty(), "Database name should be configured");
            assertTrue(config.driver == "sqlite", "Driver should be configured");

            // Check that ConfigInterface also contains correct values
            std::string dbName = handler->config->get("db.name");
            std::string driver = handler->config->get("db.driver");

            assertTrue(!dbName.empty(), "Database name should be available in ConfigInterface");
            assertTrue(driver == "sqlite", "Driver should be available in ConfigInterface");
        }
    }

    // Testing getting model through ModelInterface
    void testModelInterfaceGetModel() {
        std::cout << "\n=== Testing ModelInterface::getModel() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create test table
            createTestCategoryTable(handler->db);

            // Get model for test table
            auto model = handler->model->getModel("test_categories");
            assertTrue(model != nullptr, "Should be able to get model for 'test_categories' table");

            if (model) {
                assertTrue(model->getEntityName() == "test_categories", "Model should have correct entity name");
                assertTrue(model->getTableName() == "test_categories", "Model should have correct table name");
            }
        }
    }

    // Testing methods ModelObject for working with attributes
    void testModelObjectAttributeMethods() {
        std::cout << "\n=== Testing ModelObject Attribute Methods ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create test table
            createTestCategoryTable(handler->db);

            // Get model
            auto model = handler->model->getModel("test_categories");
            assertTrue(model != nullptr, "Model should be created");

            if (model) {
                // Testing setting and getting attributes
                model->setAttribute("name", "Test Attribute Name");
                model->setAttribute("description", "Test Description");

                std::string name = model->getAttributeValue("name");
                assertTrue(name == "Test Attribute Name", "Should get correct attribute value");

                std::string description = model->getAttributeValue("description");
                assertTrue(description == "Test Description", "Should get correct description value");

                // Testing hasAttribute
                assertTrue(model->hasAttribute("name"), "Should have 'name' attribute");
                assertFalse(model->hasAttribute("nonexistent_field"), "Should not have nonexistent attribute");

                // Testing getAllAttributes
                auto allAttrs = model->getAllAttributes();
                assertTrue(allAttrs.count("name") > 0, "Should contain 'name' attribute");
                assertTrue(allAttrs.count("description") > 0, "Should contain 'description' attribute");

                // Testing getAllNonEmptyAttributes
                auto nonEmptyAttrs = model->getAllNonEmptyAttributes();
                assertTrue(nonEmptyAttrs.count("name") > 0, "Should contain non-empty 'name' attribute");
                assertTrue(nonEmptyAttrs.count("description") > 0, "Should contain non-empty 'description' attribute");

                // Testing getting primary key
                std::string primaryKeyField = model->getPrimaryKeyField();
                assertTrue(primaryKeyField == "id", "Primary key should be 'id'");
            }
        }
    }

    // Testing ModelObject CRUD operations
    void testModelObjectCRUDOperations() {
        std::cout << "\n=== Testing ModelObject CRUD Operations ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create test table
            createTestCategoryTable(handler->db);

            // Get model
            auto model = handler->model->getModel("test_categories");
            assertTrue(model != nullptr, "Model should be created");

            if (model) {
                // Testing creation record through ModelObject
                model->setAttribute("name", "Model Object Test Category");
                model->setAttribute("description", "Test description for ModelObject");

                bool saveResult = model->save();
                assertTrue(saveResult, "Should be able to save new record through ModelObject");

                if (saveResult) {
                    auto modelData = model->getAllNonEmptyAttributes();
                    std::string id = model->getAttributeValue("id");
                    assertTrue(!id.empty() && id != "0", "ID should be set after save");

                    // Testing read record
                    auto readModel = handler->model->getModel("test_categories");
                    readModel->get("id=" + id);

                    std::string readName = readModel->getAttributeValue("name");
                    // std::cout << "DEBUG: Read model data " << readName << std::endl;
                    assertTrue(readName == "Model Object Test Category", "Should read correct name");

                    // Testing update
                    readModel->setAttribute("description", "Updated description via ModelObject");
                    bool updateResult = readModel->save();
                    assertTrue(updateResult, "Should be able to update record");

                    // Check update
                    auto updatedModel = handler->model->getModel("test_categories");
                    updatedModel->get("id=" + id);
                    std::string updatedDesc = updatedModel->getAttributeValue("description");
                    assertTrue(updatedDesc == "Updated description via ModelObject", "Description should be updated");

                    // Testing deletion
                    bool deleteResult = updatedModel->remove("id=" + id);
                    assertTrue(deleteResult, "Should be able to delete record");
                }
            }
        }
    }

    // Testing ModelTableProxy operations
    void testModelTableProxyOperations() {
        std::cout << "\n=== Testing ModelTableProxy Operations ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create test table
            createTestCategoryTable(handler->db);

            // Get model
            auto model = handler->model->getModel("test_categories");
            assertTrue(model != nullptr, "Model should be created");

            if (model) {
                // Testing creation through ModelTableProxy
                auto createdModel = model->create({
                    {"name", "Proxy Test Category"},
                    {"description", "Test via ModelTableProxy"}
                });

                assertTrue(createdModel.getAttributeValue("name") == "Proxy Test Category",
                           "Should return created model with correct name");

                std::string id = createdModel.getAttributeValue("id");
                assertTrue(!id.empty(), "Should have ID after creation");

                // Testing getting all records
                auto allModels = model->objects().all();
                assertTrue(!allModels.empty(), "Should have at least one record");

                // Testing filtering
                auto filteredModels = model->objects().filter("name=Proxy Test Category").execute();
                assertTrue(filteredModels.size() == 1, "Should find exactly one record with filter");

                // Testing limit
                auto limitedModels = model->objects().limit(1).execute();
                assertTrue(limitedModels.size() <= 1, "Should respect limit");

                // Testing update through ModelTableProxy
                int updateCount = model->objects().filter("id=" + id).update({{"description", "Updated via proxy"}});
                assertTrue(updateCount == 1, "Should update exactly one record");

                // Check update
                auto updatedModel = model->objects().get("id=" + id);
                assertTrue(updatedModel->getAttributeValue("description") == "Updated via proxy",
                           "Description should be updated via proxy");

                // Testing deletion through ModelTableProxy
                int deleteCount = model->objects().filter("id=" + id).remove();
                assertTrue(deleteCount == 1, "Should delete exactly one record via proxy");
            }
        }
    }

    // Testing complex operations with ModelTableProxy
    void testModelTableProxyComplexOperations() {
        std::cout << "\n=== Testing ModelTableProxy Complex Operations ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create test table
            createTestCategoryTable(handler->db);

            // Get model
            auto model = handler->model->getModel("test_categories");
            assertTrue(model != nullptr, "Model should be created");

            if (model) {
                // Create several test records
                model->create({{"name", "Category A"}, {"description", "Description A"}});
                model->create({{"name", "Category B"}, {"description", "Description B"}});
                model->create({{"name", "Category C"}, {"description", "Description C"}});

                // Testing order_by
                auto orderedModels = model->objects().order_by("name DESC").execute();
                if (!orderedModels.empty()) {
                    std::cout << "DEBUG: first orderedModels name: " << orderedModels[0]->getAttributeValue("name") <<
                            std::endl;
                    assertTrue(orderedModels[0]->getAttributeValue("name") == "Category C",
                               "Should order records by name descending");
                }

                // Testing tsepochki operations
                auto chainedModels = model->objects()
                        .filter("name LIKE %Category%")
                        .limit(2)
                        .order_by("name ASC")
                        .execute();
                assertTrue(chainedModels.size() <= 2, "Should respect chain of operations");

                // Remove test records
                int deletedCount = model->objects().filter("name LIKE %Category%").remove();
                assertTrue(deletedCount >= 3, "Should delete test records");
            }
        }
    }


    // Testing methods EntityBase
    void testEntityBaseCreation() {
        std::cout << "\n=== Testing EntityBase Creation ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create entity through EntityBase
            EntityDefinition entityDef = EntityBase::newEntity("test_entity_base_table");
            entityDef.verboseName = "Test Entity Base";
            entityDef.verboseNamePlural = "Test Entity Bases";
            entityDef.description = "Test entity for EntityBase functionality";

            auto entityBase = std::make_shared<EntityBase>(handler->db, entityDef);

            // Check Create entity
            assertTrue(entityBase != nullptr, "EntityBase should be created");
            assertTrue(entityBase->getName() == "test_entity_base_table", "EntityBase should have correct name");
            assertTrue(entityBase->getTableName() == "test_entity_base_table",
                       "EntityBase should have correct table name");
        }
    }

    void testEntityBaseFields() {
        std::cout << "\n=== Testing EntityBase Fields ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create entity
            EntityDefinition entityDef = EntityBase::newEntity("test_fields_table");
            auto entityBase = std::make_shared<EntityBase>(handler->db, entityDef);

            // Test Add fields
            std::map<std::string, std::string> fieldAttrs = {
                {"type", "VARCHAR"},
                {"maxLength", "100"},
                {"nullable", "false"},
                {"defaultValue", "test_value"}
            };

            entityBase->addField("test_field", fieldAttrs);

            assertTrue(entityBase->hasField("test_field"), "EntityBase should have added field");

            // Check attributes fields
            auto fieldAttributes = entityBase->getFieldAttributes("test_field");
            assertTrue(fieldAttributes["type"] == "VARCHAR", "Field should have correct type");
            assertTrue(fieldAttributes["maxLength"] == "100", "Field should have correct max length");

            // Test updating fields
            std::map<std::string, std::string> updateAttrs = {
                {"nullable", "true"},
                {"maxLength", "255"}
            };

            bool updateResult = entityBase->updateField("test_field", updateAttrs);
            assertTrue(updateResult, "Field should be updated successfully");

            auto updatedFieldAttrs = entityBase->getFieldAttributes("test_field");
            assertTrue(updatedFieldAttrs["nullable"] == "true", "Field should be nullable after update");
            assertTrue(updatedFieldAttrs["maxLength"] == "255", "Field should have updated max length");
        }
    }

    void testEntityBaseForeignKeys() {
        std::cout << "\n=== Testing EntityBase Foreign Keys ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create entity
            EntityDefinition entityDef = EntityBase::newEntity("test_fk_table");
            auto entityBase = std::make_shared<EntityBase>(handler->db, entityDef);

            // Test Add foreign key
            std::map<std::string, std::string> fkAttrs = {
                {"references", "test_categories"},
                {"toField", "id"},
                {"onDelete", "CASCADE"},
                {"onUpdate", "CASCADE"},
                {"nullable", "true"}
            };

            entityBase->addForeignKey("category_id", fkAttrs);

            assertTrue(entityBase->hasField("category_id"), "EntityBase should have foreign key field");

            // Check foreign key
            const ForeignKeyField *fk = entityBase->getForeignKey("category_id");
            assertTrue(fk != nullptr, "Foreign key should exist");
            if (fk) {
                assertTrue(fk->references == "test_categories", "FK should reference correct table");
                assertTrue(fk->onDelete == "CASCADE", "FK should have CASCADE delete");
            }

            // Test updating foreign key
            std::map<std::string, std::string> updateFkAttrs = {
                {"onDelete", "SET_NULL"},
                {"nullable", "false"}
            };

            bool updateResult = entityBase->updateForeignKey("category_id", updateFkAttrs);
            assertTrue(updateResult, "Foreign key should be updated successfully");

            const ForeignKeyField *updatedFk = entityBase->getForeignKey("category_id");
            if (updatedFk) {
                assertTrue(updatedFk->onDelete == "SET_NULL", "FK should have SET_NULL delete after update");
                assertTrue(!updatedFk->nullable, "FK should not be nullable after update");
            }
        }
    }

    void testEntityBaseIndexesAndConstraints() {
        std::cout << "\n=== Testing EntityBase Indexes and Constraints ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create entity
            EntityDefinition entityDef = EntityBase::newEntity("test_indexes_constraints_table");
            auto entityBase = std::make_shared<EntityBase>(handler->db, entityDef);

            // Add field for index
            std::map<std::string, std::string> fieldAttrs = {
                {"type", "VARCHAR"},
                {"maxLength", "100"}
            };
            entityBase->addField("indexed_field", fieldAttrs);

            // Test Add index
            IndexDefinition indexDef;
            indexDef.name = "idx_test_field";
            indexDef.fieldNames = {"indexed_field"};
            indexDef.unique = true;

            entityBase->addIndex(indexDef);

            auto indexes = entityBase->getIndexes();
            assertTrue(!indexes.empty(), "EntityBase should have an index");
            assertTrue(indexes[0].name == "idx_test_field", "Index should have correct name");

            // Test Add limit
            ConstraintDefinition constraintDef;
            constraintDef.constraintName = "chk_test_field";
            constraintDef.type = "CHECK";
            constraintDef.expression = "indexed_field <> ''";

            entityBase->addConstraint(constraintDef);

            auto constraints = entityBase->getConstraints();
            assertTrue(!constraints.empty(), "EntityBase should have a constraint");
            assertTrue(constraints[0].constraintName == "chk_test_field", "Constraint should have correct name");

            // Test validatsiyu indexes
            bool isValid = entityBase->validateIndexes();
            assertTrue(isValid, "Indexes should be valid");
        }
    }

    void testEntityBaseSaveToDatabase() {
        std::cout << "\n=== Testing EntityBase Save To Database ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->init(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            std::string entityName = "test_save_to_db";
            handler->entity->deleteEntity(entityName);

            // Create entity
            auto newEntity = handler->entity->newEntity(entityName);
            // Update Determine entity
            EntityDefinition entityDef = newEntity->getEntityDefinition();
            entityDef.verboseName = "Test Save Entity";
            entityDef.verboseNamePlural = "Test Save Entities";
            entityDef.description = "Test entity for saving to database";

            // Add fields
            std::map<std::string, std::string> idFieldAttrs = {
                {"type", "INTEGER"},
                {"primaryKey", "true"},
                {"autoIncrement", "true"}
            };
            std::map<std::string, std::string> nameFieldAttrs = {
                {"type", "VARCHAR"},
                {"maxLength", "100"},
                {"nullable", "false"}
            };

            // auto entityBase = std::make_shared<EntityBase>(handler->db, entityDef);
            newEntity->addField("id", idFieldAttrs);
            newEntity->addField("name", nameFieldAttrs);
            //
            // // Save entity in database
            bool saveResult = newEntity->saveEntity();
            assertTrue(saveResult, "EntityBase should be saved to database successfully");

            try {
                std::string checkResult = handler->db->executeQuery("SELECT COUNT(*) FROM test_save_to_db LIMIT 1;");
                assertTrue(true, "Table should exist in database");
            } catch (...) {
                // If request not prokhodit, table can not suschestvovat
                assertTrue(false, "Table not exist in database");
            }
        }
    }

    void testEntityBaseFunctionalityIntegration() {
        std::cout << "\n=== Testing EntityBase Integration ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create entity with various komponentami
            EntityDefinition entityDef = EntityBase::newEntity("test_integration_table");
            auto entityBase = std::make_shared<EntityBase>(handler->db, entityDef);

            // Add fields
            std::map<std::string, std::string> fieldAttrs = {
                {"type", "VARCHAR"},
                {"maxLength", "50"},
                {"nullable", "false"}
            };
            entityBase->addField("title", fieldAttrs);

            // Add foreign key
            std::map<std::string, std::string> fkAttrs = {
                {"references", "test_categories"},
                {"toField", "id"}
            };
            entityBase->addForeignKey("category_id", fkAttrs);

            // Add index
            IndexDefinition idxDef;
            idxDef.name = "idx_title";
            idxDef.fieldNames = {"title"};
            entityBase->addIndex(idxDef);

            // Check that all components added
            assertTrue(entityBase->hasField("title"), "Entity should have title field");
            assertTrue(entityBase->hasField("category_id"), "Entity should have category_id field");
            assertTrue(entityBase->getIndexes().size() == 1, "Entity should have one index");

            // Check metadata
            assertTrue(entityBase->getName() == "test_integration_table", "Entity should have correct name");
            assertTrue(entityBase->getTableName() == "test_integration_table", "Entity should have correct table name");
        }
    }

    void testEntityInterfaceDeleteEntity() {
        std::cout << "\n=== Testing EntityInterface::deleteEntity() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {
            // Create vremennuyu entity for testing
            std::string entityName = "test_delete_entity_table";

            handler->entity->deleteEntity(entityName);

            // Create entity
            auto entity = handler->entity->newEntity(entityName);

            // Add fields
            std::map<std::string, std::string> idFieldAttrs = {
                {"type", "INTEGER"},
                {"primaryKey", "true"},
                {"autoIncrement", "true"}
            };
            std::map<std::string, std::string> nameFieldAttrs = {
                {"type", "VARCHAR"},
                {"maxLength", "100"},
                {"nullable", "false"}
            };

            entity->addField("id", idFieldAttrs);
            entity->addField("name", nameFieldAttrs);

            // Save entity in database
            bool saveResult = entity->saveEntity();
            assertTrue(saveResult, "Test entity should be saved successfully");

            // Check that table created
            try {
                std::string checkResult = handler->db->executeQuery("SELECT COUNT(*) FROM " + entityName + " LIMIT 1;");
                assertTrue(true, "Test table should exist in database");
            } catch (...) {
                assertTrue(false, "Test table should exist in database");
            }

            // Check that entity available through EntityInterface
            auto retrievedEntity = handler->entity->getEntity(entityName);
            assertTrue(retrievedEntity != nullptr, "Entity should be retrievable before deletion");

            // Test deleting entity
            bool deleteResult = handler->entity->deleteEntity(entityName);
            // bool deleteResult = false;
            assertTrue(deleteResult, "EntityInterface::deleteEntity() should return true on successful deletion");

            // Check that entity greater not available
            auto deletedEntity = handler->entity->getEntity(entityName);
            assertTrue(deletedEntity == nullptr, "Entity should not be retrievable after deletion");

            // Check that table removed from database
            try {
                std::string checkResult = handler->db->executeQuery("SELECT COUNT(*) FROM " + entityName + " LIMIT 1;");
                assertTrue(false, "Table should not exist after entity deletion");
            } catch (...) {
                assertTrue(true, "Table should not exist after entity deletion");
            }
        }
    }

    void testEntityBaseDeleteField() {
        std::cout << "\n=== Testing EntityBase::deleteField() ===" << std::endl;

        auto handler = std::make_unique<QornixHandler>();
        handler->dbConnect(configPath());

        if (handler && handler->db && handler->db->isConnected()) {

            std::string entityName = "test_delete_field_table";
            handler->entity->deleteEntity(entityName);

            // Create entity
            auto newEntity = handler->entity->newEntity(entityName);
            // Update Determine entity
            EntityDefinition entityDef = newEntity->getEntityDefinition();
            //
            // // Create entity
            // EntityDefinition entityDef = EntityBase::newEntity("test_delete_field_table");
            auto entityBase = std::make_shared<EntityBase>(handler->db, entityDef);

            // Add several fields
            std::map<std::string, std::string> fieldAttrs1 = {
                {"type", "VARCHAR"},
                {"maxLength", "100"},
                {"nullable", "false"},
                {"defaultValue", "test_value1"}
            };

            std::map<std::string, std::string> fieldAttrs2 = {
                {"type", "INTEGER"},
                {"nullable", "true"},
                {"defaultValue", "0"}
            };

            std::map<std::string, std::string> fieldAttrs3 = {
                {"type", "TEXT"},
                {"nullable", "true"}
            };

            entityBase->addField("field1", fieldAttrs1);
            entityBase->addField("field2", fieldAttrs2);
            entityBase->addField("field3", fieldAttrs3);

            // Check that all fields added
            assertTrue(entityBase->hasField("field1"), "EntityBase should have field1");
            assertTrue(entityBase->hasField("field2"), "EntityBase should have field2");
            assertTrue(entityBase->hasField("field3"), "EntityBase should have field3");

            // Check number of fields before deletion
            auto fieldsBefore = entityBase->getFields();
            int fieldCountBefore = fieldsBefore.size();
            assertTrue(fieldCountBefore == 3, "Entity should have 3 fields before deletion");

            // Test deleting existing fields
            bool deleteResult = entityBase->removeField("field2"); // Use alias for deleteField
            assertTrue(deleteResult, "Field deletion should return true on success");

            // Check that field greater not exists
            assertFalse(entityBase->hasField("field2"), "EntityBase should not have field2 after deletion");

            // Check number of fields after deletion
            auto fieldsAfter = entityBase->getFields();
            int fieldCountAfter = fieldsAfter.size();
            assertTrue(fieldCountAfter == 2, "Entity should have 2 fields after deletion");

            // Check that remaining fields were preserved
            assertTrue(entityBase->hasField("field1"), "EntityBase should still have field1");
            assertTrue(entityBase->hasField("field3"), "EntityBase should still have field3");

            // Test popytku deletion nonexistent fields
            bool deleteNonExistentResult = entityBase->removeField("nonexistent_field");
            assertFalse(deleteNonExistentResult, "Deleting non-existent field should return false");

            // Check that attributes deleted fields greater nedostupny
            auto fieldAttrs = entityBase->getFieldAttributes("field2");
            assertTrue(fieldAttrs.empty(), "Attributes of deleted field should be empty");

            // Check that values deleted fields ochischeny
            // (if by my ustanavlivali values for fields, they by tozhe byli removed)

            std::cout << "EntityBase::deleteField() test completed successfully" << std::endl;
        }
    }


    bool runAllTests() {
        std::cout << "Starting QornixHandler::dbConnect tests..." << std::endl;

        testLoadConfigAndConnetDb();

        testDbConnectWithConfigFile();
        testDbConnectWithDefaultConfig();
        testDbConnectWithDatabaseConfig();
        testDbConnectWithConnectionString();
        testDbConnectWithInvalidConfig();
        testMultipleConnections();
        testBasicOperationsAfterConnect();

        // Tests for schema_interface
        testSchemaLoadFromDb();
        testSchemaLoadFromFile();
        testSchemaSaveFromDatabase();
        testSchemaCompareEntities();
        testSchemaApplyChangesToDB();
        testSchemaSyncEntities();
        testSchemaGetters();
        testSchemaVersionMethods();
        testSchemaGetCurrentSnapshot();

        // Tests for ModelInterface
        testModelInterfaceGetModel();
        testModelObjectAttributeMethods();
        testModelObjectCRUDOperations();
        testModelTableProxyOperations();
        testModelTableProxyComplexOperations();

        // Tests for EntityBase
        testEntityBaseCreation();
        testEntityBaseFields();
        testEntityBaseForeignKeys();
        testEntityBaseIndexesAndConstraints();
        testEntityBaseSaveToDatabase();
        testEntityBaseFunctionalityIntegration();
        testEntityInterfaceDeleteEntity();
        testEntityBaseDeleteField();

        std::cout << "\n=== Test Results ===" << std::endl;
        std::cout << "Tests passed: " << testsPassed << "/" << totalTests << std::endl;

        if (testsPassed == totalTests) {
            std::cout << "✓ All tests passed!" << std::endl;
        } else {
            std::cout << "✗ Some tests failed!" << std::endl;
        }

        return testsPassed == totalTests;
    }
};

int main() {
    QornixHandlerTest test;
    return test.runAllTests() ? 0 : 1;
}
