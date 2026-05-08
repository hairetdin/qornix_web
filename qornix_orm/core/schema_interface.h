/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include <memory>
#include "database_interface.h"
#include "app_struct.h"

class SchemaInterface {
public:
    void setDb(std::shared_ptr<DatabaseInterface> db);

    // Methods for working with the schema
    bool loadSchemaFromDb(std::shared_ptr<DatabaseInterface> db);
    bool loadSchemaFromFile(const std::string& schemaFile);
    bool saveSchemaFromDatabase(std::shared_ptr<DatabaseInterface> db, const std::string& outputFile);
    SchemaComparisonResult compareSchemaEntities();
    bool applySchemaChangesToDB(std::shared_ptr<DatabaseInterface> db);
    void syncEntitiesWithDatabase();
    SchemaSnapshot getCurrentSnapshot();
    std::string getCurrentSchemaVersion();
    // bool incrementSchemaVersion();
    std::vector<std::string> getSchemaVersionHistory();

    // Other methods from SchemaLoader that need to be exposed
    const std::map<std::string, EntityDefinition>& getEntities();
    const std::map<std::string, EntityDefinition>& getDbEntities();
    const std::vector<ViewDefinition>& getViews();
    const std::vector<MaterializedViewDefinition>& getMaterializedViews();
    const std::vector<StoredProcedureDefinition>& getStoredProcedures();
    const std::vector<TriggerDefinition>& getTriggers();
    const std::vector<DatabaseFunctionDefinition>& getDatabaseFunctions();
    const ConfigurationDefinition& getConfiguration();
    const SchemaMetadata& getSchemaMetadata();
};
