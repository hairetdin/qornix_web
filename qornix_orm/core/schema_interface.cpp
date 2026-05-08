/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_interface.h"
#include "schema_loader.h"

void SchemaInterface::setDb(std::shared_ptr<DatabaseInterface> db) {
    return SchemaLoader::setDb(db);
}

bool SchemaInterface::loadSchemaFromDb(std::shared_ptr<DatabaseInterface> db) {
    return SchemaLoader::loadSchemaFromDB(db);
}

bool SchemaInterface::loadSchemaFromFile(const std::string& schemaFile) {
    return SchemaLoader::loadSchemaFromFile(schemaFile);
}

bool SchemaInterface::saveSchemaFromDatabase(std::shared_ptr<DatabaseInterface> db, const std::string& outputFile) {
    return SchemaLoader::saveSchemaFromDatabase(db, outputFile);
}

SchemaComparisonResult SchemaInterface::compareSchemaEntities() {
    return SchemaLoader::compareSchemaEntities();
}

bool SchemaInterface::applySchemaChangesToDB(std::shared_ptr<DatabaseInterface> db) {
    return SchemaLoader::applySchemaChangesToDB(db);
}

void SchemaInterface::syncEntitiesWithDatabase() {
    SchemaLoader::syncEntitiesWithDatabase();
}

SchemaSnapshot SchemaInterface::getCurrentSnapshot() {
    return SchemaLoader::getCurrentSnapshot();
}

std::string SchemaInterface::getCurrentSchemaVersion() {
    return SchemaLoader::getCurrentSchemaVersion();
}

// bool SchemaInterface::incrementSchemaVersion() {
//     return SchemaLoader::incrementSchemaVersion();
// }

std::vector<std::string> SchemaInterface::getSchemaVersionHistory() {
    return SchemaLoader::getSchemaVersionHistory();
}

const std::map<std::string, EntityDefinition>& SchemaInterface::getEntities() {
    return SchemaLoader::getEntities();
}

const std::map<std::string, EntityDefinition>& SchemaInterface::getDbEntities() {
    return SchemaLoader::getDbEntities();
}

const std::vector<ViewDefinition>& SchemaInterface::getViews() {
    return SchemaLoader::getViews();
}

const std::vector<MaterializedViewDefinition>& SchemaInterface::getMaterializedViews() {
    return SchemaLoader::getMaterializedViews();
}

const std::vector<StoredProcedureDefinition>& SchemaInterface::getStoredProcedures() {
    return SchemaLoader::getStoredProcedures();
}

const std::vector<TriggerDefinition>& SchemaInterface::getTriggers() {
    return SchemaLoader::getTriggers();
}

const std::vector<DatabaseFunctionDefinition>& SchemaInterface::getDatabaseFunctions() {
    return SchemaLoader::getDatabaseFunctions();
}

const ConfigurationDefinition& SchemaInterface::getConfiguration() {
    return SchemaLoader::getConfiguration();
}

const SchemaMetadata& SchemaInterface::getSchemaMetadata() {
    return SchemaLoader::getSchemaMetadata();
}
