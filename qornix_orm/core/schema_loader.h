/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include <string>

#include "app_struct.h"
#include "database_interface.h"

class DBMigrator;

class SchemaLoader {
private:
    static std::shared_ptr<DBMigrator> dbMigrator_;
    static DatabaseMappingDefinition databaseMapping_;
    // Fields for tracking versions
    static SchemaMetadata schemaMetadata_;
    static SchemaSnapshot currentSnapshot_;
    static bool saveSchemaMetadataToFile(const SchemaMetadata& metadata, const std::string& filename);
    static SchemaMetadata loadSchemaMetadataFromFile(const std::string& filename);

    // Methods for loading schema from database
    static bool loadSchemaFromPostgreSQL(std::shared_ptr<DatabaseInterface> db);
    static bool loadSchemaFromMySQL(std::shared_ptr<DatabaseInterface> db);
    static bool loadSchemaFromSQLite(std::shared_ptr<DatabaseInterface> db);
    static std::string mapDBTypeToAppType(const std::string& dbType, const std::string& dbEngine);
    static std::vector<std::map<std::string, std::string>> parseTabDelimitedResult(const std::string &result);

    static std::string getDbEngine();

public:
    static std::shared_ptr<DatabaseInterface> db;
    static void setDb(std::shared_ptr<DatabaseInterface> db);

    // schema instances data
    static std::map<std::string, EntityDefinition> entities_;
    static std::map<std::string, EntityDefinition> dbEntities_;
    static std::vector<ViewDefinition> views_;
    static std::vector<ViewDefinition> dbViews_;
    static std::vector<MaterializedViewDefinition> materializedViews_;
    static std::vector<MaterializedViewDefinition> dbMaterializedViews_;
    static std::vector<StoredProcedureDefinition> storedProcedures_;
    static std::vector<StoredProcedureDefinition> dbStoredProcedures_;
    static std::vector<TriggerDefinition> triggers_;
    static std::vector<TriggerDefinition> dbTriggers_;
    static std::vector<DatabaseFunctionDefinition> databaseFunctions_;
    static std::vector<DatabaseFunctionDefinition> dbDatabaseFunctions_;
    static std::vector<SequenceDefinition> sequences_;
    static std::vector<SequenceDefinition> dbSequences_;

    static ConfigurationDefinition configuration_;
    // Load application schema from XML file following schema_app.xsd
    static bool loadSchemaFromFile(const std::string& schemaFile);
    // Method for loading schema from database
    static bool loadSchemaFromDB(std::shared_ptr<DatabaseInterface> db);
    static void setDBMigrator(std::shared_ptr<DBMigrator> migrator);

    static void syncEntitiesWithDatabase();

    static bool saveSchemaFromDatabase(const std::shared_ptr<DatabaseInterface>& db,
                                       const std::string &outputFile);

    /**
     * Compares the application schema with database
     * @return SchemaComparisonResult containing detailed comparison information
     */
    static SchemaComparisonResult compareSchemaEntities();
    static SchemaComparisonResult compareSchemaViews(SchemaComparisonResult result);

    // Methods for working with versions schema
    static SchemaMetadata loadSchemaMetadata(const std::string& schemaFile);
    static SchemaSnapshot getCurrentSnapshot();
    static SchemaSnapshot createCurrentSnapshot();
    // static void updateCurrentSnapshot();
    static std::string calculateSchemaHash(const SchemaSnapshot &snapshot);
    static std::string getCurrentSchemaVersion();
    // static bool incrementSchemaVersion();
    static std::vector<std::string> getSchemaVersionHistory();

    // Get metadata current schema
    static const SchemaMetadata& getSchemaMetadata();

    // Getters for schema elements
    static const std::string& getApplicationName();
    static const ConfigurationDefinition& getConfiguration();
    static const std::map<std::string, EntityDefinition>& getEntities();
    static const std::map<std::string, EntityDefinition>& getDbEntities();
    static const std::vector<ViewDefinition>& getViews();
    static const std::vector<MaterializedViewDefinition>& getMaterializedViews();
    static const std::vector<StoredProcedureDefinition>& getStoredProcedures();
    static const std::vector<TriggerDefinition>& getTriggers();
    static const std::vector<DatabaseFunctionDefinition>& getDatabaseFunctions();
    static const DatabaseMappingDefinition& getDatabaseMapping();

    // Process DataStructureType from schema
    static void processDataStructure();

    // Methods for update metadata
    static bool updateSchemaMetadata(const SchemaMetadata& newMetadata);
    static bool updateSchemaVersion(const std::string& newVersion);
    static void setSchemaMetadata(const SchemaMetadata& metadata);

    // Method for checks need update
    static void processEntityReverseRelationships(const EntityDefinition &entity,
                                           std::map<std::string, EntityDefinition> &allEntities);
    static void processReverseRelationships(std::map<std::string, EntityDefinition>& entities);

    static bool isSchemaMetadataUpdateNeeded(const SchemaMetadata& currentMetadata);

    static bool applySchemaChangesToDB(std::shared_ptr<DatabaseInterface> db);

    static bool createDBTable(std::shared_ptr<DatabaseInterface> db, const EntityDefinition &entity);

    static std::string generateCreateTableSQL(const EntityDefinition &entity, const std::string &dbEngine);

    static std::string mapAppTypeToDBType(const std::string& appType, const std::string& dbEngine,
                                          const std::string& precision = "", const std::string& scale = "",
                                          const std::string& maxLength = "255");

    static bool addDBField(std::shared_ptr<DatabaseInterface> db, const std::string &tableName,
                    const FieldDefinition &field);

    static std::string
    generateAddFieldSQL(const std::string &tableName, const FieldDefinition &field, const std::string &dbEngine);

    static bool modifyDBField(std::shared_ptr<DatabaseInterface> db, const std::string &tableName,
                       const FieldDefinition &field);

    static std::string
    generateModifyFieldSQL(const std::string &tableName, const FieldDefinition &field, const std::string &dbEngine);

    static bool addDBForeignKey(std::shared_ptr<DatabaseInterface> db, const std::string &tableName,
                                const ForeignKeyField &fk);

    static void extractPostgreSQLTables(std::shared_ptr<DatabaseInterface> db);
    static void extractPostgreSQLViews(std::shared_ptr<DatabaseInterface> db);
    static void extractPostgreSQLMaterializedViews(std::shared_ptr<DatabaseInterface> db);
    static void extractPostgreSQLFunctions(std::shared_ptr<DatabaseInterface> db);
    static void extractPostgreSQLTriggers(std::shared_ptr<DatabaseInterface> db);
    static void extractPostgreSQLSequences(std::shared_ptr<DatabaseInterface> db);

    static void extractPostgreSQLIndexes(std::shared_ptr<DatabaseInterface> db);
};
