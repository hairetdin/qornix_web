/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_loader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <utility>

#include "handler_interface.h"
#include "entity_interface.h"
#include "xml_schema_validator.h"

ConfigurationDefinition SchemaLoader::configuration_;
std::shared_ptr<DatabaseInterface> SchemaLoader::db = nullptr;
SchemaMetadata SchemaLoader::schemaMetadata_;
SchemaSnapshot SchemaLoader::currentSnapshot_;
std::shared_ptr<DBMigrator> SchemaLoader::dbMigrator_ = nullptr;
DatabaseMappingDefinition SchemaLoader::databaseMapping_;
std::map<std::string, EntityDefinition> SchemaLoader::entities_;
std::map<std::string, EntityDefinition> SchemaLoader::dbEntities_;
std::vector<ViewDefinition> SchemaLoader::views_;
std::vector<ViewDefinition> SchemaLoader::dbViews_;
std::vector<MaterializedViewDefinition> SchemaLoader::materializedViews_;
std::vector<MaterializedViewDefinition> SchemaLoader::dbMaterializedViews_;
std::vector<StoredProcedureDefinition> SchemaLoader::storedProcedures_;
std::vector<StoredProcedureDefinition> SchemaLoader::dbStoredProcedures_;
std::vector<TriggerDefinition> SchemaLoader::triggers_;
std::vector<TriggerDefinition> SchemaLoader::dbTriggers_;
std::vector<DatabaseFunctionDefinition> SchemaLoader::databaseFunctions_;
std::vector<DatabaseFunctionDefinition> SchemaLoader::dbDatabaseFunctions_;
std::vector<SequenceDefinition> SchemaLoader::sequences_;
std::vector<SequenceDefinition> SchemaLoader::dbSequences_;

namespace {

void appendText(pugi::xml_node parent, const char* name, const std::string& value) {
    parent.append_child(name).text() = value.c_str();
}

void appendTextIfNotEmpty(pugi::xml_node parent, const char* name, const std::string& value) {
    if (!value.empty()) {
        appendText(parent, name, value);
    }
}

void appendAttributeIfNotEmpty(pugi::xml_node node, const char* name, const std::string& value) {
    if (!value.empty()) {
        node.append_attribute(name) = value.c_str();
    }
}

void appendFieldAttributes(pugi::xml_node fieldNode, const FieldDefinition& field) {
    fieldNode.append_attribute("name") = field.name.c_str();
    fieldNode.append_attribute("type") = field.type.c_str();
    fieldNode.append_attribute("nullable") = field.nullable;
    fieldNode.append_attribute("primaryKey") = field.primaryKey;
    fieldNode.append_attribute("unique") = field.unique;

    appendAttributeIfNotEmpty(fieldNode, "maxLength", field.maxLength);
    appendAttributeIfNotEmpty(fieldNode, "defaultValue", field.defaultValue);
    appendAttributeIfNotEmpty(fieldNode, "verboseName", field.verboseName);
    appendAttributeIfNotEmpty(fieldNode, "description", field.description);
    fieldNode.append_attribute("autoIncrement") = field.autoIncrement;
    appendAttributeIfNotEmpty(fieldNode, "collation", field.collation);
    appendAttributeIfNotEmpty(fieldNode, "computedExpression", field.computedExpression);
    fieldNode.append_attribute("isComputed") = field.isComputed;
    appendAttributeIfNotEmpty(fieldNode, "precision", field.precision);
    appendAttributeIfNotEmpty(fieldNode, "scale", field.scale);

    if (!field.enumValues.empty()) {
        auto enumValuesNode = fieldNode.append_child("EnumValues");
        for (const auto& value : field.enumValues) {
            appendText(enumValuesNode, "Value", value);
        }
    }
}

void appendParameter(pugi::xml_node parametersNode, const ParameterDefinition& parameter, bool includeMode) {
    auto parameterNode = parametersNode.append_child("Parameter");
    appendAttributeIfNotEmpty(parameterNode, "name", parameter.name);
    parameterNode.append_attribute("type") = parameter.type.c_str();
    if (includeMode) {
        appendAttributeIfNotEmpty(parameterNode, "mode", parameter.mode);
    }
    parameterNode.append_attribute("required") = parameter.required;
}

void appendFunction(pugi::xml_node functionsNode, const FunctionDefinition& function) {
    auto functionNode = functionsNode.append_child("Function");
    appendAttributeIfNotEmpty(functionNode, "fileName", function.fileName);
    appendAttributeIfNotEmpty(functionNode, "language", function.language);
    appendText(functionNode, "Name", function.name);
    appendTextIfNotEmpty(functionNode, "Description", function.description);

    if (!function.parameters.empty()) {
        auto parametersNode = functionNode.append_child("Parameters");
        for (const auto& parameter : function.parameters) {
            appendParameter(parametersNode, parameter, false);
        }
    }

    appendTextIfNotEmpty(functionNode, "ReturnType", function.returnType);
    appendTextIfNotEmpty(functionNode, "Code", function.code);
}

void appendOptions(pugi::xml_node parent, const std::map<std::string, std::string>& options) {
    if (options.empty()) {
        return;
    }

    auto optionsNode = parent.append_child("Options");
    for (const auto& [key, value] : options) {
        auto optionNode = optionsNode.append_child("Option");
        optionNode.append_attribute("key") = key.c_str();
        optionNode.append_attribute("value") = value.c_str();
    }
}

void appendPartitions(pugi::xml_node entityNode, const std::vector<PartitionDefinition>& partitions) {
    if (partitions.empty()) {
        return;
    }

    auto partitionsNode = entityNode.append_child("Partitions");
    const auto& partition = partitions.front();
    appendText(partitionsNode, "Strategy", partition.strategy);

    auto columnsNode = partitionsNode.append_child("Columns");
    for (const auto& column : partition.columns) {
        appendText(columnsNode, "Column", column);
    }

    appendTextIfNotEmpty(partitionsNode, "Expression", partition.expression);

    if (!partition.bounds.empty()) {
        auto boundsNode = partitionsNode.append_child("Bounds");
        for (const auto& bound : partition.bounds) {
            auto boundNode = boundsNode.append_child("Bound");
            appendText(boundNode, "PartitionName", bound.partitionName);
            appendTextIfNotEmpty(boundNode, "LowerBound", bound.lowerBound);
            appendTextIfNotEmpty(boundNode, "UpperBound", bound.upperBound);
            if (!bound.values.empty()) {
                auto valuesNode = boundNode.append_child("Values");
                for (const auto& value : bound.values) {
                    appendText(valuesNode, "Value", value);
                }
            }
        }
    }
}

void appendConstraint(pugi::xml_node entityNode, const ConstraintDefinition& constraint) {
    auto constraintNode = entityNode.append_child("Constraint");
    constraintNode.append_attribute("constraintName") = constraint.constraintName.c_str();
    constraintNode.append_attribute("type") = constraint.type.c_str();
    appendText(constraintNode, "Name", constraint.name);
    appendTextIfNotEmpty(constraintNode, "Description", constraint.description);
    appendText(constraintNode, "Expression", constraint.expression);
}

void appendCheckConstraint(pugi::xml_node entityNode, const CheckConstraintDefinition& checkConstraint) {
    ConstraintDefinition constraint;
    constraint.name = checkConstraint.name;
    constraint.constraintName = checkConstraint.name;
    constraint.type = "CHECK";
    constraint.description = checkConstraint.description;
    constraint.expression = checkConstraint.expression;
    appendConstraint(entityNode, constraint);
}

void appendViews(pugi::xml_node dataStructureNode, const std::vector<ViewDefinition>& views) {
    if (views.empty()) {
        return;
    }

    auto viewsNode = dataStructureNode.append_child("Views");
    for (const auto& view : views) {
        auto viewNode = viewsNode.append_child("View");
        viewNode.append_attribute("viewName") = view.viewName.c_str();
        viewNode.append_attribute("isUpdatable") = view.isUpdatable;
        appendText(viewNode, "Name", view.name);
        appendTextIfNotEmpty(viewNode, "Description", view.description);
        appendText(viewNode, "Query", view.query);
    }
}

void appendMaterializedViews(
    pugi::xml_node dataStructureNode,
    const std::vector<MaterializedViewDefinition>& materializedViews
) {
    if (materializedViews.empty()) {
        return;
    }

    auto viewsNode = dataStructureNode.append_child("MaterializedViews");
    for (const auto& view : materializedViews) {
        auto viewNode = viewsNode.append_child("MaterializedView");
        viewNode.append_attribute("viewName") = view.viewName.c_str();
        viewNode.append_attribute("withData") = view.withData;
        appendText(viewNode, "Name", view.name);
        appendTextIfNotEmpty(viewNode, "Description", view.description);
        appendText(viewNode, "Query", view.query);
        appendTextIfNotEmpty(viewNode, "RefreshStrategy", view.refreshStrategy);
    }
}

void appendDatabaseFunctions(
    pugi::xml_node dataStructureNode,
    const std::vector<DatabaseFunctionDefinition>& functions
) {
    if (functions.empty()) {
        return;
    }

    auto functionsNode = dataStructureNode.append_child("DatabaseFunctions");
    for (const auto& function : functions) {
        auto functionNode = functionsNode.append_child("DatabaseFunction");
        functionNode.append_attribute("functionName") = function.functionName.c_str();
        appendAttributeIfNotEmpty(functionNode, "language", function.language);
        appendAttributeIfNotEmpty(functionNode, "volatile", function.volatileType);
        appendAttributeIfNotEmpty(functionNode, "security", function.security);
        appendText(functionNode, "Name", function.name);
        appendTextIfNotEmpty(functionNode, "Description", function.description);
        appendText(functionNode, "ReturnType", function.returnType);
        appendText(functionNode, "Code", function.code);
    }
}

void appendStoredProcedures(
    pugi::xml_node dataStructureNode,
    const std::vector<StoredProcedureDefinition>& procedures
) {
    if (procedures.empty()) {
        return;
    }

    auto proceduresNode = dataStructureNode.append_child("StoredProcedures");
    for (const auto& procedure : procedures) {
        auto procedureNode = proceduresNode.append_child("StoredProcedure");
        procedureNode.append_attribute("procedureName") = procedure.procedureName.c_str();
        appendAttributeIfNotEmpty(procedureNode, "language", procedure.language);
        appendAttributeIfNotEmpty(procedureNode, "security", procedure.security);
        appendText(procedureNode, "Name", procedure.name);
        appendTextIfNotEmpty(procedureNode, "Description", procedure.description);
        appendTextIfNotEmpty(procedureNode, "ReturnType", procedure.returnType);
        appendTextIfNotEmpty(procedureNode, "Code", procedure.code);
    }
}

std::string normalizeSchemaEnumToken(std::string value);

void appendTriggers(pugi::xml_node dataStructureNode, const std::vector<TriggerDefinition>& triggers) {
    if (triggers.empty()) {
        return;
    }

    auto triggersNode = dataStructureNode.append_child("Triggers");
    for (const auto& trigger : triggers) {
        auto triggerNode = triggersNode.append_child("Trigger");
        triggerNode.append_attribute("triggerName") = trigger.triggerName.c_str();
        triggerNode.append_attribute("tableName") = trigger.tableName.c_str();
        triggerNode.append_attribute("enabled") = trigger.enabled;
        appendText(triggerNode, "Name", trigger.name);
        appendTextIfNotEmpty(triggerNode, "Description", trigger.description);
        appendText(triggerNode, "Event", normalizeSchemaEnumToken(trigger.event));
        appendText(triggerNode, "Timing", normalizeSchemaEnumToken(trigger.timing));
        appendText(triggerNode, "Function", trigger.function);
        appendTextIfNotEmpty(triggerNode, "Condition", trigger.condition);
    }
}

void appendSequences(pugi::xml_node dataStructureNode, const std::vector<SequenceDefinition>& sequences) {
    if (sequences.empty()) {
        return;
    }

    auto sequencesNode = dataStructureNode.append_child("Sequences");
    for (const auto& sequence : sequences) {
        auto sequenceNode = sequencesNode.append_child("Sequence");
        sequenceNode.append_attribute("name") = sequence.name.c_str();
        appendAttributeIfNotEmpty(sequenceNode, "tableName", sequence.tableName);
        appendAttributeIfNotEmpty(sequenceNode, "columnName", sequence.columnName);
        sequenceNode.append_attribute("startValue") = sequence.startValue;
        sequenceNode.append_attribute("increment") = sequence.increment;
        sequenceNode.append_attribute("minValue") = sequence.minValue;
        sequenceNode.append_attribute("maxValue") = sequence.maxValue;
        sequenceNode.append_attribute("cycle") = sequence.cycle;
        sequenceNode.append_attribute("cache") = sequence.cache;
    }
}

std::string typeAttributeForEngine(const std::string& dbEngine) {
    if (dbEngine == "postgresql") {
        return "postgresqlType";
    }
    if (dbEngine == "mysql") {
        return "mysqlType";
    }
    if (dbEngine == "sqlite" || dbEngine == "sqlite3") {
        return "sqliteType";
    }
    return {};
}

void appendFieldMapping(
    pugi::xml_node entityMappingNode,
    const std::string& fieldName,
    const std::string& columnName,
    const std::string& dbType,
    const std::string& dbEngine
) {
    auto fieldMappingNode = entityMappingNode.append_child("FieldMapping");
    fieldMappingNode.append_attribute("fieldName") = fieldName.c_str();
    fieldMappingNode.append_attribute("columnName") = columnName.c_str();

    const std::string typeAttribute = typeAttributeForEngine(dbEngine);
    if (!typeAttribute.empty() && !dbType.empty()) {
        fieldMappingNode.append_attribute(typeAttribute.c_str()) = dbType.c_str();
    }
}

std::string normalizeForeignKeyAction(std::string action) {
    std::transform(action.begin(), action.end(), action.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    std::replace(action.begin(), action.end(), ' ', '_');
    return action;
}

std::string normalizeSchemaEnumToken(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    std::replace(value.begin(), value.end(), ' ', '_');
    return value;
}

std::string normalizeDatabaseEngine(const std::string& driver) {
    if (driver == "sqlite3") {
        return "sqlite";
    }
    return driver;
}

std::string redactKeyValue(std::string value, const std::string& key) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    const std::string needle = key + "=";
    size_t pos = lower.find(needle);
    while (pos != std::string::npos) {
        const size_t valueStart = pos + needle.size();
        size_t valueEnd = value.find_first_of(" ;&", valueStart);
        if (valueEnd == std::string::npos) {
            valueEnd = value.size();
        }
        value.replace(valueStart, valueEnd - valueStart, "***");
        lower.replace(valueStart, valueEnd - valueStart, "***");
        pos = lower.find(needle, valueStart + 3);
    }

    return value;
}

std::string sanitizeConnectionString(std::string connectionString) {
    if (connectionString.empty()) {
        return connectionString;
    }

    const size_t schemeEnd = connectionString.find("://");
    const size_t at = connectionString.find('@', schemeEnd == std::string::npos ? 0 : schemeEnd + 3);
    if (schemeEnd != std::string::npos && at != std::string::npos) {
        const size_t credentialsStart = schemeEnd + 3;
        const size_t passwordSeparator = connectionString.find(':', credentialsStart);
        if (passwordSeparator != std::string::npos && passwordSeparator < at) {
            connectionString.replace(passwordSeparator + 1, at - passwordSeparator - 1, "***");
        }
    }

    connectionString = redactKeyValue(connectionString, "password");
    connectionString = redactKeyValue(connectionString, "pwd");
    return connectionString;
}

void appendDatabaseMapping(
    pugi::xml_node appNode,
    const DatabaseMappingDefinition& databaseMapping,
    const std::map<std::string, EntityDefinition>& entities,
    const std::string& dbEngine
) {
    auto mappingNode = appNode.append_child("DatabaseMapping");
    bool hasMapping = false;

    for (const auto& entityMapping : databaseMapping.entityMappings) {
        auto entityMappingNode = mappingNode.append_child("EntityMapping");
        entityMappingNode.append_attribute("entityName") = entityMapping.entityName.c_str();
        entityMappingNode.append_attribute("tableName") = entityMapping.tableName.c_str();
        for (const auto& fieldMapping : entityMapping.fieldMappings) {
            auto fieldMappingNode = entityMappingNode.append_child("FieldMapping");
            fieldMappingNode.append_attribute("fieldName") = fieldMapping.fieldName.c_str();
            fieldMappingNode.append_attribute("columnName") = fieldMapping.columnName.c_str();
            appendAttributeIfNotEmpty(fieldMappingNode, "postgresqlType", fieldMapping.postgresqlType);
            appendAttributeIfNotEmpty(fieldMappingNode, "mysqlType", fieldMapping.mysqlType);
            appendAttributeIfNotEmpty(fieldMappingNode, "sqliteType", fieldMapping.sqliteType);
        }
        hasMapping = true;
    }

    if (!databaseMapping.typeMapping.typeMaps.empty()) {
        auto typeMappingNode = mappingNode.append_child("TypeMapping");
        for (const auto& typeMap : databaseMapping.typeMapping.typeMaps) {
            auto typeMapNode = typeMappingNode.append_child("TypeMap");
            typeMapNode.append_attribute("appType") = typeMap.appType.c_str();
            appendAttributeIfNotEmpty(typeMapNode, "postgresqlType", typeMap.postgresqlType);
            appendAttributeIfNotEmpty(typeMapNode, "mysqlType", typeMap.mysqlType);
            appendAttributeIfNotEmpty(typeMapNode, "sqliteType", typeMap.sqliteType);
        }
        hasMapping = true;
    }

    if (databaseMapping.entityMappings.empty()) {
        for (const auto& [entityName, entity] : entities) {
            auto entityMappingNode = mappingNode.append_child("EntityMapping");
            entityMappingNode.append_attribute("entityName") = entityName.c_str();
            entityMappingNode.append_attribute("tableName") = entity.tableName.c_str();

            std::set<std::string> mappedColumns;
            for (const auto& field : entity.fields) {
                appendFieldMapping(entityMappingNode, field.name, field.name, field.type, dbEngine);
                mappedColumns.insert(field.name);
            }

            for (const auto& fk : entity.foreignKeys) {
                if (mappedColumns.find(fk.name) == mappedColumns.end()) {
                    appendFieldMapping(entityMappingNode, fk.name, fk.name, fk.type, dbEngine);
                    mappedColumns.insert(fk.name);
                }
            }
            hasMapping = true;
        }
    }

    if (!hasMapping) {
        appNode.remove_child(mappingNode);
    }
}

} // namespace

void SchemaLoader::setDb(std::shared_ptr<DatabaseInterface> db_) {
    SchemaLoader::db = std::move(db_);
}

const std::string& SchemaLoader::getApplicationName() {
    return schemaMetadata_.name;
}

const ConfigurationDefinition& SchemaLoader::getConfiguration() {
    return configuration_;
}

const std::map<std::string, EntityDefinition>& SchemaLoader::getEntities() {
    return entities_;
}

const std::map<std::string, EntityDefinition>& SchemaLoader::getDbEntities() {
    return dbEntities_;
}

const std::vector<ViewDefinition>& SchemaLoader::getViews() {
    return views_;
}

const std::vector<MaterializedViewDefinition>& SchemaLoader::getMaterializedViews() {
    return materializedViews_;
}

const std::vector<StoredProcedureDefinition>& SchemaLoader::getStoredProcedures() {
    return storedProcedures_;
}

const std::vector<TriggerDefinition>& SchemaLoader::getTriggers() {
    return triggers_;
}

const std::vector<DatabaseFunctionDefinition>& SchemaLoader::getDatabaseFunctions() {
    return databaseFunctions_;
}

const DatabaseMappingDefinition& SchemaLoader::getDatabaseMapping() {
    return databaseMapping_;
}

void SchemaLoader::setDBMigrator(std::shared_ptr<DBMigrator> migrator) {
    dbMigrator_ = std::move(migrator);
}

void SchemaLoader::processEntityReverseRelationships(
    const EntityDefinition& entity,
    std::map<std::string, EntityDefinition>& allEntities
    ) {
    // Process each foreign key in entity
    for (const auto& fk : entity.foreignKeys) {
        std::string relatedName;

        if (!fk.relatedName.empty()) {
            // Use specified Name for reverse relation
            relatedName = fk.relatedName;
        } else {
            // Generate Name by default: name_references
            relatedName = fk.name;

            // Remove suffix _id if it is
            if (relatedName.length() > 3 &&
                relatedName.substr(relatedName.length() - 3) == "_id") {
                relatedName = relatedName.substr(0, relatedName.length() - 3);
                }

            // Build Name: name_references (fk_name__table_name__set)
            relatedName += "__" + fk.references + "__set";
        }

        // Create reverse relation
        FieldDefinition reverseField;
        reverseField.name = relatedName;
        reverseField.type = "RELATED_KEYS";
        reverseField.isReverseRelation = true;
        reverseField.references = entity.tableName;

        // Find target entity and add in it reverse relation
        auto targetEntityIt = allEntities.find(fk.references);
        if (targetEntityIt != allEntities.end()) {
            targetEntityIt->second.reverseRelations.push_back(reverseField);
        }
    }
}

void SchemaLoader::processReverseRelationships(std::map<std::string, EntityDefinition>& entities) {
    for (auto& [entityName, entity] : entities) {
        processEntityReverseRelationships(entity, entities);
    }
}

std::string SchemaLoader::getDbEngine() {
    auto dbConfig =db -> getDatabaseConfig();
    std::string dbEngine = dbConfig.driver;
    return dbEngine;
}

bool SchemaLoader::loadSchemaFromDB(std::shared_ptr<DatabaseInterface> db_interface) {
    try {
        if (!db_interface) {
            std::cerr << "Database interface is null" << std::endl;
            return false;
        }
        db = db_interface;

        // Determine type database from configuration
        // auto dbConfig =db_interface -> getDatabaseConfig();
        // std::string dbEngine = dbConfig.driver;
        auto dbEngine =  getDbEngine();

        // Cleanup existing data
        dbEntities_.clear();
        dbViews_.clear();
        dbMaterializedViews_.clear();
        dbStoredProcedures_.clear();
        dbTriggers_.clear();
        dbDatabaseFunctions_.clear();
        dbSequences_.clear();

//        std::cout << "DEBUG: Cleared existing schema data" << std::endl;

        bool result = false;
        if (dbEngine == "postgresql") {
            result = loadSchemaFromPostgreSQL(db_interface);
        } else if (dbEngine == "mysql") {
            result = loadSchemaFromMySQL(db_interface);
        } else if (dbEngine == "sqlite") {
            result = loadSchemaFromSQLite(db_interface);
        } else {
            std::cerr << "Unsupported database engine: " << dbEngine << std::endl;
            return false;
        }
        if (result) {
            processReverseRelationships(dbEntities_);
            std::cout << "DEBUG: loadSchemaFromDB loaded successfully" << std::endl;
        }
        return result;
    } catch (const std::exception& e) {
        std::cerr << "Error loading schema from database: " << e.what() << std::endl;
        return false;
    }
}

// Parse rows with tabs. zero string is treated as header.
std::vector<std::map<std::string, std::string>> SchemaLoader::parseTabDelimitedResult(const std::string& result) {
    std::vector<std::map<std::string, std::string>> parsedResults;

    if (result.empty()) {
        return parsedResults;
    }

    std::istringstream stream(result);
    std::string line;
    std::vector<std::string> lines;

    // Split input into lines
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    if (lines.empty()) {
        return parsedResults;
    }

    // Check if we have header line
    if (!lines.empty()) {
        std::istringstream headerStream(lines[0]);
        std::string headerCell;
        std::vector<std::string> headers;

        // Parse header line
        while (std::getline(headerStream, headerCell, '\t')) {
            headers.push_back(headerCell);
        }

        // Process data rows (skip header)
        for (size_t i = 1; i < lines.size(); i++) {
            std::istringstream dataStream(lines[i]);
            std::string cell;
            std::vector<std::string> cells;

            // Parse data line
            while (std::getline(dataStream, cell, '\t')) {
                cells.push_back(cell);
            }

            // Map cells to headers
            std::map<std::string, std::string> row;
            for (size_t j = 0; j < headers.size() && j < cells.size(); j++) {
                row[headers[j]] = cells[j];
            }

            // Handle case where we have more cells than headers
            for (size_t j = headers.size(); j < cells.size(); j++) {
                row["column_" + std::to_string(j)] = cells[j];
            }

            parsedResults.push_back(row);
        }
    }

    return parsedResults;
}


void SchemaLoader::extractPostgreSQLTables(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get all tables in the public schema
        std::string tablesQuery = "SELECT tablename FROM pg_tables WHERE schemaname='public'";

        std::string tablesResult = db->executeQueryWithHeaders(tablesQuery);

//        std::cout << "DEBUG: Raw tablesResult: '" << tablesResult << "'" << std::endl;

        std::vector<std::map<std::string, std::string>> tableResults = parseTabDelimitedResult(tablesResult);

        // Debug output for tableResults
//        std::cout << "DEBUG: tableResults size: " << tableResults.size() << std::endl;
//        for (size_t i = 0; i < tableResults.size(); ++i) {
//            std::cout << "DEBUG: tableResults[" << i << "]:" << std::endl;
//            for (const auto& pair : tableResults[i]) {
//                std::cout << "  " << pair.first << " = " << pair.second << std::endl;
//            }
//        }

        for (const auto& tableRow : tableResults) {

            std::string tableName = tableRow.at("tablename");
            EntityDefinition entity;
            entity.name = tableName;
            entity.tableName = tableName;

            // Get table comments
            std::string tableCommentQuery = R"(
                SELECT obj_description(c.oid) as comment
                FROM pg_class c
                JOIN pg_namespace n ON n.oid = c.relnamespace
                WHERE c.relname = ')" + tableName + R"('
                AND n.nspname = 'public'
            )";

            std::string commentResult = db->executeQueryWithHeaders(tableCommentQuery);
            std::vector<std::map<std::string, std::string>> commentResults = parseTabDelimitedResult(commentResult);
            if (!commentResults.empty() && !commentResults[0].at("comment").empty()) {
                entity.description = commentResults[0].at("comment");
            }

            // Get column information
            std::string columnsQuery = R"(
                SELECT
                    a.attname as column_name,
                    pg_catalog.format_type(a.atttypid, a.atttypmod) as data_type,
                    a.attnotnull as not_null,
                    a.atthasdef as has_default,
                    pg_get_expr(d.adbin, d.adrelid) as default_value,
                    col_description(a.attrelid, a.attnum) as column_comment,
                    a.attidentity as identity,
                    a.attgenerated as generated
                FROM pg_attribute a
                LEFT JOIN pg_attrdef d ON a.attrelid = d.adrelid AND a.attnum = d.adnum
                JOIN pg_class c ON a.attrelid = c.oid
                JOIN pg_namespace n ON c.relnamespace = n.oid
                WHERE c.relname = ')" + tableName + R"('
                AND n.nspname = 'public'
                AND a.attnum > 0
                AND NOT a.attisdropped
                ORDER BY a.attnum
            )";

            std::string columnsResult = db->executeQueryWithHeaders(columnsQuery);
            std::vector<std::map<std::string, std::string>> columnResults = parseTabDelimitedResult(columnsResult);

            for (const auto& columnRow : columnResults) {
                FieldDefinition field;
                field.name = columnRow.at("column_name");
                field.type = mapDBTypeToAppType(columnRow.at("data_type"), "postgresql");

                // Set nullability
                field.nullable = (columnRow.at("not_null") == "f");

                // Set default value
                if (columnRow.find("default_value") != columnRow.end() && !columnRow.at("default_value").empty()) {
                    std::string defaultValue = columnRow.at("default_value");

                    // Check is whether value automatically generated
                    if (!defaultValue.empty() &&
                        defaultValue != "CURRENT_TIMESTAMP" &&
                        defaultValue != "NOW()" &&
                        defaultValue.find("nextval") == std::string::npos &&
                        defaultValue.find("uuid_generate") == std::string::npos &&
                        defaultValue.find("gen_random_uuid") == std::string::npos) {
                        field.defaultValue = defaultValue;
                    }
                }

                // Set description/comment
                if (columnRow.find("column_comment") != columnRow.end() && !columnRow.at("column_comment").empty()) {
                    field.description = columnRow.at("column_comment");
                }

                // Check for identity/auto-increment
                if (columnRow.find("identity") != columnRow.end() && !columnRow.at("identity").empty()) {
                    field.autoIncrement = true;
                }

                // Check for generated column
                if (columnRow.find("generated") != columnRow.end() && columnRow.at("generated") == "s") {
                    field.isComputed = true;
                }

                entity.fields.push_back(field);
            }

            // Get primary key information
            std::string pkQuery = R"(
                SELECT a.attname as column_name
                FROM pg_index i
                JOIN pg_class c ON c.oid = i.indrelid
                JOIN pg_namespace n ON n.oid = c.relnamespace
                JOIN pg_attribute a ON a.attrelid = c.oid AND a.attnum = ANY(i.indkey)
                WHERE c.relname = ')" + tableName + R"('
                AND n.nspname = 'public'
                AND i.indisprimary
                ORDER BY a.attnum
            )";

            std::string pkResult = db->executeQueryWithHeaders(pkQuery);
            std::vector<std::map<std::string, std::string>> pkResults = parseTabDelimitedResult(pkResult);

            for (const auto& pkRow : pkResults) {
                std::string pkColumnName = pkRow.at("column_name");
                // Mark the field as primary key
                for (auto& field : entity.fields) {
                    if (field.name == pkColumnName) {
                        field.primaryKey = true;
                        break;
                    }
                }
            }

            // Get unique constraints
            std::string uniqueQuery = R"(
                SELECT
                    con.conname as constraint_name,
                    a.attname as column_name
                FROM pg_constraint con
                JOIN pg_class c ON c.oid = con.conrelid
                JOIN pg_namespace n ON n.oid = c.relnamespace
                JOIN pg_attribute a ON a.attrelid = c.oid AND a.attnum = ANY(con.conkey)
                WHERE c.relname = ')" + tableName + R"('
                AND n.nspname = 'public'
                AND con.contype = 'u'
                ORDER BY con.conname, a.attnum
            )";

            std::string uniqueResult = db->executeQueryWithHeaders(uniqueQuery);
            std::vector<std::map<std::string, std::string>> uniqueResults = parseTabDelimitedResult(uniqueResult);

            // Process unique constraints (simplified - marking individual fields as unique)
            for (const auto& uniqueRow : uniqueResults) {
                std::string uniqueColumnName = uniqueRow.at("column_name");
                for (auto& field : entity.fields) {
                    if (field.name == uniqueColumnName) {
                        field.unique = true;
                        break;
                    }
                }
            }

            // Get foreign key information
            std::string fkQuery = R"(
                SELECT
                    kcu.column_name,
                    ccu.table_name AS references_table,
                    ccu.column_name AS references_column,
                    rc.update_rule,
                    rc.delete_rule,
                    con.conname AS constraint_name
                FROM information_schema.table_constraints tc
                JOIN information_schema.key_column_usage kcu
                    ON tc.constraint_name = kcu.constraint_name
                    AND tc.table_schema = kcu.table_schema
                JOIN information_schema.constraint_column_usage ccu
                    ON ccu.constraint_name = tc.constraint_name
                    AND ccu.table_schema = tc.table_schema
                JOIN information_schema.referential_constraints rc
                    ON tc.constraint_name = rc.constraint_name
                JOIN pg_constraint con
                    ON con.conname = tc.constraint_name
                WHERE tc.constraint_type = 'FOREIGN KEY'
                    AND tc.table_schema = 'public'
                    AND tc.table_name = ')" + tableName + R"('
            )";

            std::string fkResult = db->executeQueryWithHeaders(fkQuery);
            std::vector<std::map<std::string, std::string>> fkResults = parseTabDelimitedResult(fkResult);

            for (const auto& fkRow : fkResults) {
                ForeignKeyField fkField;
                fkField.name = fkRow.at("column_name");
                fkField.references = fkRow.at("references_table");
                fkField.toField = fkRow.at("references_column");
                fkField.onUpdate = fkRow.at("update_rule");
                fkField.onDelete = fkRow.at("delete_rule");

                // Copy base field properties
                for (const auto& field : entity.fields) {
                    if (field.name == fkField.name) {
                        fkField.type = field.type;
                        fkField.nullable = field.nullable;
                        fkField.primaryKey = field.primaryKey;
                        fkField.unique = field.unique;
                        fkField.maxLength = field.maxLength;
                        fkField.defaultValue = field.defaultValue;
                        fkField.verboseName = field.verboseName;
                        fkField.description = field.description;
                        fkField.autoIncrement = field.autoIncrement;
                        fkField.collation = field.collation;
                        fkField.computedExpression = field.computedExpression;
                        fkField.isComputed = field.isComputed;
                        fkField.precision = field.precision;
                        fkField.scale = field.scale;
                        fkField.enumValues = field.enumValues;
                        break;
                    }
                }

                entity.foreignKeys.push_back(fkField);
            }

            // Get check constraints
            std::string checkQuery = R"(
                SELECT
                    con.conname as constraint_name,
                    pg_get_constraintdef(con.oid) as constraint_def
                FROM pg_constraint con
                JOIN pg_class c ON c.oid = con.conrelid
                JOIN pg_namespace n ON n.oid = c.relnamespace
                WHERE c.relname = ')" + tableName + R"('
                AND n.nspname = 'public'
                AND con.contype = 'c'
            )";

            std::string checkResult = db->executeQueryWithHeaders(checkQuery);
            std::vector<std::map<std::string, std::string>> checkResults = parseTabDelimitedResult(checkResult);

            for (const auto& checkRow : checkResults) {
                CheckConstraintDefinition checkConstraint;
                checkConstraint.name = checkRow.at("constraint_name");
                checkConstraint.expression = checkRow.at("constraint_def");
                entity.checkConstraints.push_back(checkConstraint);
            }

            // Get indexes
            std::string indexQuery = R"(
                SELECT
                    ix.relname as index_name,
                    a.attname as column_name,
                    i.indisunique as is_unique,
                    am.amname as index_method
                FROM pg_class t
                JOIN pg_namespace n ON n.oid = t.relnamespace
                JOIN pg_index i ON t.oid = i.indrelid
                JOIN pg_class ix ON ix.oid = i.indexrelid
                JOIN pg_am am ON am.oid = ix.relam
                JOIN pg_attribute a ON a.attrelid = t.oid AND a.attnum = ANY(i.indkey)
                WHERE t.relname = ')" + tableName + R"('
                AND n.nspname = 'public'
                AND NOT i.indisprimary
                ORDER BY ix.relname, a.attnum
            )";

            std::string indexResult = db->executeQueryWithHeaders(indexQuery);
            std::vector<std::map<std::string, std::string>> indexResults = parseTabDelimitedResult(indexResult);

            std::map<std::string, IndexDefinition> indexMap;
            for (const auto& indexRow : indexResults) {
                std::string indexName = indexRow.at("index_name");

                if (indexMap.find(indexName) == indexMap.end()) {
                    IndexDefinition index;
                    index.name = indexName;
                    index.unique = (indexRow.at("is_unique") == "t");
                    index.method = indexRow.at("index_method");
                    indexMap[indexName] = index;
                }

                indexMap[indexName].fieldNames.push_back(indexRow.at("column_name"));
            }

            for (const auto& pair : indexMap) {
                entity.indexes.push_back(pair.second);
            }

            // Check if table is partitioned
            std::string partitionQuery = R"(
                SELECT partstrat
                FROM pg_partitioned_table pt
                JOIN pg_class c ON pt.partrelid = c.oid
                JOIN pg_namespace n ON n.oid = c.relnamespace
                WHERE c.relname = ')" + tableName + R"('
                AND n.nspname = 'public'
            )";

            std::string partitionResult = db->executeQueryWithHeaders(partitionQuery);
            std::vector<std::map<std::string, std::string>> partitionResults = parseTabDelimitedResult(partitionResult);

            if (!partitionResults.empty()) {
                entity.isPartitioned = true;
            }

            // Get tablespace information
            std::string tablespaceQuery = R"(
                SELECT t.spcname
                FROM pg_class c
                JOIN pg_namespace n ON n.oid = c.relnamespace
                LEFT JOIN pg_tablespace t ON c.reltablespace = t.oid
                WHERE c.relname = ')" + tableName + R"('
                AND n.nspname = 'public'
            )";

            std::string tablespaceResult = db->executeQueryWithHeaders(tablespaceQuery);
            std::vector<std::map<std::string, std::string>> tablespaceResults = parseTabDelimitedResult(tablespaceResult);

            if (!tablespaceResults.empty() && !tablespaceResults[0].at("spcname").empty()) {
                entity.tablespace = tablespaceResults[0].at("spcname");
            }

            // Store the entity
            dbEntities_[entity.name] = entity;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting PostgreSQL tables: " << e.what() << std::endl;
        throw;
    }
}

void SchemaLoader::extractPostgreSQLViews(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get all views in the public schema
        std::string viewsQuery = R"(
            SELECT
                viewname,
                replace(replace(definition, E'\r', ' '), E'\n', ' ') as definition,
                CASE
                    WHEN pg_relation_is_updatable(c.oid, false) & 32 = 32 THEN 'true'
                    ELSE 'false'
                END AS is_updatable
            FROM pg_views v
            JOIN pg_class c ON c.relname = v.viewname
            JOIN pg_namespace n ON n.oid = c.relnamespace
            WHERE v.schemaname = 'public'
        )";

        std::string viewsResult = db->executeQueryWithHeaders(viewsQuery);
        std::vector<std::map<std::string, std::string>> viewResults = parseTabDelimitedResult(viewsResult);

        for (const auto& viewRow : viewResults) {
            ViewDefinition view;
            view.name = viewRow.at("viewname");
            view.viewName = viewRow.at("viewname");
            view.query = viewRow.at("definition");
            view.isUpdatable = (viewRow.at("is_updatable") == "true");

            // Get view comment if exists
            std::string viewCommentQuery = R"(
                SELECT obj_description(c.oid) as comment
                FROM pg_class c
                JOIN pg_namespace n ON n.oid = c.relnamespace
                WHERE c.relname = ')" + view.viewName + R"('
                AND n.nspname = 'public'
                AND c.relkind = 'v'
            )";

            std::string commentResult = db->executeQueryWithHeaders(viewCommentQuery);
            std::vector<std::map<std::string, std::string>> commentResults = parseTabDelimitedResult(commentResult);
            if (!commentResults.empty() && !commentResults[0].at("comment").empty()) {
                view.description = commentResults[0].at("comment");
            }

            dbViews_.push_back(view);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting PostgreSQL views: " << e.what() << std::endl;
        throw;
    }
}

void SchemaLoader::extractPostgreSQLMaterializedViews(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get all materialized views in the public schema
        std::string matViewsQuery = R"(
            SELECT
                mv.relname as matviewname,
                replace(replace(pg_get_viewdef(mv.oid), E'\r', ' '), E'\n', ' ') as definition,
               CASE
                    WHEN mv.relkind = 'm' THEN 'true'
                    ELSE 'false'
                END AS with_data
            FROM pg_class mv
            JOIN pg_namespace n ON n.oid = mv.relnamespace
            WHERE mv.relkind = 'm'
            AND n.nspname = 'public'
        )";

        std::string matViewsResult = db->executeQueryWithHeaders(matViewsQuery);
        std::vector<std::map<std::string, std::string>> matViewResults = parseTabDelimitedResult(matViewsResult);

        for (const auto& matViewRow : matViewResults) {
            MaterializedViewDefinition matView;
            matView.name = matViewRow.at("matviewname");
            matView.viewName = matViewRow.at("matviewname");
            matView.query = matViewRow.at("definition");
            matView.withData = (matViewRow.at("with_data") == "true");
            matView.refreshStrategy = "MANUAL"; // Default, can be updated based on refresh policies if needed

            // Get materialized view comment if exists
            std::string matViewCommentQuery = R"(
                SELECT obj_description(c.oid) as comment
                FROM pg_class c
                JOIN pg_namespace n ON n.oid = c.relnamespace
                WHERE c.relname = ')" + matView.viewName + R"('
                AND n.nspname = 'public'
                AND c.relkind = 'm'
            )";

            std::string commentResult = db->executeQueryWithHeaders(matViewCommentQuery);
            std::vector<std::map<std::string, std::string>> commentResults = parseTabDelimitedResult(commentResult);
            if (!commentResults.empty() && !commentResults[0].at("comment").empty()) {
                matView.description = commentResults[0].at("comment");
            }

            dbMaterializedViews_.push_back(matView);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting PostgreSQL materialized views: " << e.what() << std::endl;
        throw;
    }
}

void SchemaLoader::extractPostgreSQLFunctions(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get all functions in the public schema
        std::string functionsQuery = R"(
            SELECT
                p.proname as name,
                replace(replace(pg_get_functiondef(p.oid), E'\r', ' '), E'\n', ' ') as function_definition,
                pg_get_function_arguments(p.oid) as function_arguments,
                pg_get_function_result(p.oid) as return_type,
                l.lanname as language,
                CASE
                    WHEN p.provolatile = 'i' THEN 'IMMUTABLE'
                    WHEN p.provolatile = 's' THEN 'STABLE'
                    ELSE 'VOLATILE'
                END as volatile_type,
                CASE
                    WHEN p.prosecdef THEN 'DEFINER'
                    ELSE 'INVOKER'
                END as security_type
            FROM pg_proc p
            JOIN pg_namespace n ON p.pronamespace = n.oid
            JOIN pg_language l ON p.prolang = l.oid
            WHERE n.nspname = 'public'
            AND p.prokind = 'f'
        )";

        std::string functionsResult = db->executeQueryWithHeaders(functionsQuery);
        std::vector<std::map<std::string, std::string>> functionResults = parseTabDelimitedResult(functionsResult);

        for (const auto& funcRow : functionResults) {
            DatabaseFunctionDefinition dbFunc;
            dbFunc.name = funcRow.at("name");
            dbFunc.functionName = funcRow.at("name");
            dbFunc.code = funcRow.at("function_definition");
            dbFunc.returnType = funcRow.at("return_type");
            dbFunc.language = funcRow.at("language");
            dbFunc.volatileType = funcRow.at("volatile_type");
            dbFunc.security = funcRow.at("security_type");

            // Get function comment if exists
            std::string funcCommentQuery = R"(
                SELECT obj_description(p.oid) as comment
                FROM pg_proc p
                JOIN pg_namespace n ON p.pronamespace = n.oid
                WHERE p.proname = ')" + dbFunc.functionName + R"('
                AND n.nspname = 'public'
            )";

            std::string commentResult = db->executeQueryWithHeaders(funcCommentQuery);
            std::vector<std::map<std::string, std::string>> commentResults = parseTabDelimitedResult(commentResult);
            if (!commentResults.empty() && !commentResults[0].at("comment").empty()) {
                dbFunc.description = commentResults[0].at("comment");
            }

            dbDatabaseFunctions_.push_back(dbFunc);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting PostgreSQL functions: " << e.what() << std::endl;
        throw;
    }
}

void SchemaLoader::extractPostgreSQLTriggers(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get all triggers in the public schema
        std::string triggersQuery = R"(
            SELECT
                t.tgname as trigger_name,
                c.relname as table_name,
                replace(replace(pg_get_triggerdef(t.oid), E'\r', ' '), E'\n', ' ') as trigger_definition,
                CASE
                    WHEN t.tgtype & 2 = 2 THEN 'BEFORE'
                    WHEN t.tgtype & 64 = 64 THEN 'INSTEAD OF'
                    ELSE 'AFTER'
                END as timing,
                CASE
                    WHEN t.tgtype & 8 = 8 THEN 'INSERT'
                    WHEN t.tgtype & 16 = 16 THEN 'UPDATE'
                    WHEN t.tgtype & 32 = 32 THEN 'DELETE'
                    WHEN t.tgtype & 512 = 512 THEN 'TRUNCATE'
                    ELSE ''
                END as event,
                p.proname as function_name,
                CASE
                    WHEN t.tgenabled = 'O' THEN 'true'
                    ELSE 'false'
                END as is_enabled
            FROM pg_trigger t
            JOIN pg_class c ON t.tgrelid = c.oid
            JOIN pg_namespace n ON c.relnamespace = n.oid
            JOIN pg_proc p ON t.tgfoid = p.oid
            WHERE n.nspname = 'public'
            AND NOT t.tgisinternal
        )";

        std::string triggersResult = db->executeQueryWithHeaders(triggersQuery);
        std::vector<std::map<std::string, std::string>> triggerResults = parseTabDelimitedResult(triggersResult);

        for (const auto& triggerRow : triggerResults) {
            TriggerDefinition trigger;
            trigger.name = triggerRow.at("trigger_name");
            trigger.triggerName = triggerRow.at("trigger_name");
            trigger.tableName = triggerRow.at("table_name");
            trigger.event = triggerRow.at("event");
            trigger.timing = triggerRow.at("timing");
            trigger.function = triggerRow.at("function_name");
            trigger.enabled = (triggerRow.at("is_enabled") == "true");

            // Extract function definition for the trigger
            std::string funcDefQuery = R"(
                SELECT replace(replace(pg_get_functiondef(p.oid), E'\r', ' '), E'\n', ' ') as function_definition
                FROM pg_proc p
                WHERE p.proname = ')" + trigger.function + R"('
            )";

            std::string funcDefResult = db->executeQueryWithHeaders(funcDefQuery);
            std::vector<std::map<std::string, std::string>> funcDefResults = parseTabDelimitedResult(funcDefResult);
            if (!funcDefResults.empty()) {
                trigger.condition = funcDefResults[0].at("function_definition");
            }

            // Get trigger comment if exists
            std::string triggerCommentQuery = R"(
                SELECT obj_description(t.oid) as comment
                FROM pg_trigger t
                JOIN pg_class c ON t.tgrelid = c.oid
                JOIN pg_namespace n ON c.relnamespace = n.oid
                WHERE t.tgname = ')" + trigger.triggerName + R"('
                AND n.nspname = 'public'
            )";

            std::string commentResult = db->executeQueryWithHeaders(triggerCommentQuery);
            std::vector<std::map<std::string, std::string>> commentResults = parseTabDelimitedResult(commentResult);
            if (!commentResults.empty() && !commentResults[0].at("comment").empty()) {
                trigger.description = commentResults[0].at("comment");
            }

            dbTriggers_.push_back(trigger);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting PostgreSQL triggers: " << e.what() << std::endl;
        throw;
    }
}

void SchemaLoader::extractPostgreSQLSequences(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get all sequences in the public schema
        std::string sequencesQuery = R"(
            SELECT
                s.relname as sequence_name,
                a.attname as column_name,
                pg_get_serial_sequence(c.relname, a.attname) as sequence_oid,
                COALESCE(pg_get_expr(ad.adbin, ad.adrelid), '') as default_value
            FROM pg_class s
            JOIN pg_namespace n ON n.oid = s.relnamespace
            LEFT JOIN pg_depend d ON d.refobjid = s.oid AND d.deptype = 'a'
            LEFT JOIN pg_class c ON c.oid = d.objid
            LEFT JOIN pg_attribute a ON a.attrelid = c.oid AND a.attnum = d.objsubid
            LEFT JOIN pg_attrdef ad ON ad.adrelid = c.oid AND ad.adnum = a.attnum
            WHERE s.relkind = 'S'
            AND n.nspname = 'public'
        )";

        std::string sequencesResult = db->executeQueryWithHeaders(sequencesQuery);
        std::vector<std::map<std::string, std::string>> sequenceResults = parseTabDelimitedResult(sequencesResult);

        std::map<std::string, SequenceDefinition> sequenceMap;

        for (const auto& seqRow : sequenceResults) {
            std::string sequenceName = seqRow.at("sequence_name");

            if (sequenceMap.find(sequenceName) == sequenceMap.end()) {
                SequenceDefinition sequence;
                sequence.name = sequenceName;

                // Get detailed sequence information
                std::string seqDetailsQuery = R"(
                    SELECT
                        start_value,
                        increment_by,
                        min_value,
                        max_value,
                        cache_size,
                        cycle
                    FROM pg_sequences
                    WHERE schemaname = 'public'
                    AND sequencename = ')" + sequenceName + R"('
                )";
//                std::cout << "DEBUG: Raw seqDetailsQuery: '" << seqDetailsQuery << "'" << std::endl;

                std::string seqDetailsResult = db->executeQueryWithHeaders(seqDetailsQuery);
//                std::cout << "DEBUG: Raw seqDetailsResult: '" << seqDetailsResult << "'" << std::endl;

                std::vector<std::map<std::string, std::string>> seqDetailsResults = parseTabDelimitedResult(seqDetailsResult);

                // Debug output for seqDetailsResults
//                std::cout << "DEBUG: seqDetailsResults size: " << seqDetailsResults.size() << std::endl;
//                for (size_t i = 0; i < seqDetailsResults.size(); ++i) {
//                    std::cout << "DEBUG: seqDetailsResults[" << i << "]:" << std::endl;
//                    for (const auto& pair : seqDetailsResults[i]) {
//                        std::cout << "  " << pair.first << " = " << pair.second << std::endl;
//                    }
//                }

                if (!seqDetailsResults.empty()) {
                    const auto& details = seqDetailsResults[0];
                    sequence.startValue = std::stoll(details.at("start_value"));
                    sequence.increment = std::stoll(details.at("increment_by"));
                    sequence.minValue = std::stoll(details.at("min_value"));
                    sequence.maxValue = std::stoll(details.at("max_value"));
                    sequence.cache = std::stoll(details.at("cache_size"));
                    sequence.cycle = (details.at("cycle") == "t");
                }

                sequenceMap[sequenceName] = sequence;
            }

            // Associate sequence with table and column if available
            if (!seqRow.at("column_name").empty()) {
                sequenceMap[sequenceName].columnName = seqRow.at("column_name");
            }

            // if (!seqRow.at("default_value").empty()) {
            //     // Extract table name from default value (e.g., nextval('table_column_seq'::regclass))
            //     std::string defaultValue = seqRow.at("default_value");
            //     size_t start = defaultValue.find("nextval('");
            //     if (start != std::string::npos) {
            //         start += 9; // Length of "nextval('"
            //         size_t end = defaultValue.find("'", start);
            //         if (end != std::string::npos) {
            //             std::string seqRef = defaultValue.substr(start, end - start);
            //             size_t dotPos = seqRef.find(".");
            //             if (dotPos != std::string::npos) {
            //                 sequenceMap[sequenceName].tableName = seqRef.substr(0, dotPos);
            //             }
            //         }
            //     }
            // }
        }

        // Add all sequences to the sequences_ container
        for (const auto& pair : sequenceMap) {
            dbSequences_.push_back(pair.second);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting PostgreSQL sequences: " << e.what() << std::endl;
        throw;
    }
}

void SchemaLoader::extractPostgreSQLIndexes(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get all indexes in the public schema
        std::string indexesQuery = R"(
            SELECT
                ix.relname as index_name,
                t.relname as table_name,
                a.attname as column_name,
                i.indisunique as is_unique,
                i.indisprimary as is_primary,
                am.amname as index_method,
                pg_get_indexdef(i.indexrelid, 0, true) as index_definition,
                pg_get_expr(i.indpred, i.indrelid) as where_clause
            FROM pg_class t
            JOIN pg_namespace n ON n.oid = t.relnamespace
            JOIN pg_index i ON t.oid = i.indrelid
            JOIN pg_class ix ON ix.oid = i.indexrelid
            JOIN pg_am am ON am.oid = ix.relam
            JOIN pg_attribute a ON a.attrelid = t.oid AND a.attnum = ANY(i.indkey)
            WHERE n.nspname = 'public'
            AND NOT i.indisprimary
            ORDER BY ix.relname, a.attnum
        )";

        std::string indexesResult = db->executeQueryWithHeaders(indexesQuery);
        std::cout << "DEBUG: Raw indexesResult: '" << indexesResult << "'" << std::endl;

        std::vector<std::map<std::string, std::string>> indexResults = parseTabDelimitedResult(indexesResult);

        // Debug output for indexResults
        std::cout << "DEBUG: indexResults size: " << indexResults.size() << std::endl;
        for (size_t i = 0; i < indexResults.size(); ++i) {
            std::cout << "DEBUG: indexResults[" << i << "]:" << std::endl;
            for (const auto& pair : indexResults[i]) {
                std::cout << "  " << pair.first << " = " << pair.second << std::endl;
            }
        }

        // Group index information by index name
        std::map<std::string, IndexDefinition> indexMap;
        std::map<std::string, std::string> indexTables; // To track which table each index belongs to

        for (const auto& indexRow : indexResults) {
            std::string indexName = indexRow.at("index_name");
            std::string tableName = indexRow.at("table_name");

            if (indexMap.find(indexName) == indexMap.end()) {
                IndexDefinition index;
                index.name = indexName;
                index.unique = (indexRow.at("is_unique") == "t");
                index.method = indexRow.at("index_method");
                index.type = indexRow.at("index_method");

                // Check if it's a partial index
//                if (!indexRow.at("where_clause").empty()) {
//                    index.isPartial = true;
//                    index.whereClause = indexRow.at("where_clause");
//                }

                indexMap[indexName] = index;
                indexTables[indexName] = tableName;
            }

            // Add column to the index
            indexMap[indexName].fieldNames.push_back(indexRow.at("column_name"));
        }

        // Store indexes in the appropriate entities
        for (const auto& pair : indexMap) {
            const std::string& indexName = pair.first;
            const IndexDefinition& index = pair.second;
            const std::string& tableName = indexTables[indexName];

            // Find the entity this index belongs to
            auto entityIt = dbEntities_.find(tableName);
            if (entityIt != dbEntities_.end()) {
                // Add index to the entity
                entityIt->second.indexes.push_back(index);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting PostgreSQL indexes: " << e.what() << std::endl;
        throw;
    }
}

bool SchemaLoader::loadSchemaFromPostgreSQL(std::shared_ptr<DatabaseInterface> db) {
    try {
        std::cout << "DEBUG: Starting loadSchemaFromPostgreSQL" << std::endl;

        // Extract tables with complete structure
        extractPostgreSQLTables(db);

        // Extract views
        extractPostgreSQLViews(db);

        // Extract materialized views
        extractPostgreSQLMaterializedViews(db);

        // Extract functions
        extractPostgreSQLFunctions(db);

        // Extract triggers
        extractPostgreSQLTriggers(db);

        // Extract sequences
        extractPostgreSQLSequences(db);

        // Extract indexes
        extractPostgreSQLIndexes(db);

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading schema from PostgreSQL: " << e.what() << std::endl;
        return false;
    }
}

bool SchemaLoader::loadSchemaFromMySQL(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get table information using executeQuery
        std::string tablesQuery = "SELECT table_name FROM information_schema.tables WHERE table_schema = DATABASE()";
        std::string tablesResult = db->executeQuery(tablesQuery);

        // Parse table results
        std::vector<std::map<std::string, std::string>> tableResults;
        std::istringstream tablesStream(tablesResult);
        std::string line;
        while (std::getline(tablesStream, line)) {
            std::map<std::string, std::string> row;
            row["table_name"] = line;
            tableResults.push_back(row);
        }

        for (const auto& tableRow : tableResults) {
            std::string tableName = tableRow.at("table_name");

            EntityDefinition entity;
            entity.tableName = tableName;
            entity.name = tableName;

            // Get column information using executeQuery
            std::string columnsQuery = "SELECT column_name, data_type, is_nullable, column_default "
                                     "FROM information_schema.columns "
                                     "WHERE table_name = '" + tableName + "' AND table_schema = DATABASE()";
            std::string columnsResult = db->executeQuery(columnsQuery);

            // Parse column results
            std::vector<std::map<std::string, std::string>> columnResults;
            std::istringstream columnsStream(columnsResult);
            std::string columnLine;
            while (std::getline(columnsStream, columnLine)) {
                std::map<std::string, std::string> row;
                std::istringstream columnLineStream(columnLine);
                std::string column_name, data_type, is_nullable, column_default;
                std::getline(columnLineStream, column_name, '\t');
                std::getline(columnLineStream, data_type, '\t');
                std::getline(columnLineStream, is_nullable, '\t');
                std::getline(columnLineStream, column_default);
                row["column_name"] = column_name;
                row["data_type"] = data_type;
                row["is_nullable"] = is_nullable;
                row["column_default"] = column_default;
                columnResults.push_back(row);
            }

            for (const auto& columnRow : columnResults) {
                FieldDefinition field;
                field.name = columnRow.at("column_name");
                field.type = mapDBTypeToAppType(columnRow.at("data_type"), "mysql");
                field.nullable = (columnRow.at("is_nullable") == "YES");

                // Check if field is a primary key
                std::string pkQuery = "SELECT column_name FROM information_schema.key_column_usage "
                                    "WHERE table_schema = DATABASE() "
                                    "AND table_name = '" + tableName + "' "
                                    "AND column_name = '" + field.name + "' "
                                    "AND constraint_name = 'PRIMARY'";
                std::string pkResultStr = db->executeQuery(pkQuery);

                // Parse PK result
                std::vector<std::map<std::string, std::string>> pkResult;
                std::istringstream pkStream(pkResultStr);
                std::string pkLine;
                while (std::getline(pkStream, pkLine)) {
                    std::map<std::string, std::string> row;
                    row["column_name"] = pkLine;
                    pkResult.push_back(row);
                }

                if (!pkResult.empty()) {
                    field.primaryKey = true;
                }

                // Set default value
                if (columnRow.find("column_default") != columnRow.end() && !columnRow.at("column_default").empty()) {
                    field.defaultValue = columnRow.at("column_default");

                    // Check for auto increment
                    std::string extraQuery = "SELECT extra FROM information_schema.columns "
                                           "WHERE table_schema = DATABASE() "
                                           "AND table_name = '" + tableName + "' "
                                           "AND column_name = '" + field.name + "'";
                    std::string extraResult = db->executeQuery(extraQuery);
                    if (extraResult.find("auto_increment") != std::string::npos) {
                        field.autoIncrement = true;
                    }
                }

                entity.fields.push_back(field);
            }

            // Get foreign key information using executeQuery
            std::string fkQuery = "SELECT kcu.column_name, kcu.referenced_table_name, "
                                "kcu.referenced_column_name, rc.update_rule, rc.delete_rule "
                                "FROM information_schema.key_column_usage kcu "
                                "JOIN information_schema.referential_constraints rc "
                                "ON kcu.constraint_name = rc.constraint_name "
                                "WHERE kcu.table_name = '" + tableName + "' "
                                "AND kcu.table_schema = DATABASE() "
                                "AND kcu.referenced_table_name IS NOT NULL";
            std::string fkResultStr = db->executeQuery(fkQuery);

            // Parse FK results
            std::vector<std::map<std::string, std::string>> fkResults;
            std::istringstream fkStream(fkResultStr);
            std::string fkLine;
            while (std::getline(fkStream, fkLine)) {
                std::map<std::string, std::string> row;
                std::istringstream fkLineStream(fkLine);
                std::string column_name, referenced_table_name, referenced_column_name, update_rule, delete_rule;
                std::getline(fkLineStream, column_name, '\t');
                std::getline(fkLineStream, referenced_table_name, '\t');
                std::getline(fkLineStream, referenced_column_name, '\t');
                std::getline(fkLineStream, update_rule, '\t');
                std::getline(fkLineStream, delete_rule);
                row["column_name"] = column_name;
                row["referenced_table_name"] = referenced_table_name;
                row["referenced_column_name"] = referenced_column_name;
                row["update_rule"] = update_rule;
                row["delete_rule"] = delete_rule;
                fkResults.push_back(row);
            }

            for (const auto& fkRow : fkResults) {
                ForeignKeyField fkField;
                fkField.name = fkRow.at("column_name");
                fkField.references = fkRow.at("referenced_table_name");
                fkField.toField = fkRow.at("referenced_column_name");
                fkField.onUpdate = fkRow.at("update_rule");
                fkField.onDelete = fkRow.at("delete_rule");
                fkField.type = "FOREIGN_KEY";

                entity.foreignKeys.push_back(fkField);
            }

            // Get index information using executeQuery
            std::string indexQuery = "SELECT index_name, column_name, non_unique "
                                   "FROM information_schema.statistics "
                                   "WHERE table_name = '" + tableName + "' AND index_name != 'PRIMARY' "
                                   "AND table_schema = DATABASE()";
            std::string indexResultStr = db->executeQuery(indexQuery);

            // Parse index results
            std::vector<std::map<std::string, std::string>> indexResults;
            std::istringstream indexStream(indexResultStr);
            std::string indexLine;
            while (std::getline(indexStream, indexLine)) {
                std::map<std::string, std::string> row;
                std::istringstream indexLineStream(indexLine);
                std::string index_name, column_name, non_unique;
                std::getline(indexLineStream, index_name, '\t');
                std::getline(indexLineStream, column_name, '\t');
                std::getline(indexLineStream, non_unique);
                row["index_name"] = index_name;
                row["column_name"] = column_name;
                row["non_unique"] = non_unique;
                indexResults.push_back(row);
            }

            std::map<std::string, IndexDefinition> indexMap;
            for (const auto& indexRow : indexResults) {
                std::string indexName = indexRow.at("index_name");

                if (indexMap.find(indexName) == indexMap.end()) {
                    IndexDefinition index;
                    index.name = indexName;
                    index.unique = (indexRow.at("non_unique") == "0");
                    indexMap[indexName] = index;
                }

                indexMap[indexName].fieldNames.push_back(indexRow.at("column_name"));
            }

            for (const auto& pair : indexMap) {
                entity.indexes.push_back(pair.second);
            }

            // Get CHECK constraints (MySQL 8.0.16+)
            std::string checkConstraintsQuery = "SELECT constraint_name, check_clause "
                                              "FROM information_schema.check_constraints "
                                              "WHERE table_name = '" + tableName + "' "
                                              "AND constraint_schema = DATABASE()";
            std::string checkResult = db->executeQuery(checkConstraintsQuery);

            // Parse CHECK constraints
            std::vector<std::map<std::string, std::string>> checkResults;
            std::istringstream checkStream(checkResult);
            std::string checkLine;
            while (std::getline(checkStream, checkLine)) {
                std::map<std::string, std::string> row;
                std::istringstream checkLineStream(checkLine);
                std::string constraint_name, check_clause;
                std::getline(checkLineStream, constraint_name, '\t');
                std::getline(checkLineStream, check_clause);
                row["constraint_name"] = constraint_name;
                row["check_clause"] = check_clause;
                checkResults.push_back(row);
            }

            for (const auto& checkRow : checkResults) {
                CheckConstraintDefinition checkConstraint;
                checkConstraint.name = checkRow.at("constraint_name");
                checkConstraint.expression = checkRow.at("check_clause");
                entity.checkConstraints.push_back(checkConstraint);
            }

            // Get table options (engine, charset, etc.)
            std::string tableOptionsQuery = "SELECT engine, table_collation, row_format "
                                          "FROM information_schema.tables "
                                          "WHERE table_name = '" + tableName + "' "
                                          "AND table_schema = DATABASE()";
            std::string tableOptionsResult = db->executeQuery(tableOptionsQuery);

            if (!tableOptionsResult.empty()) {
                std::istringstream optionsStream(tableOptionsResult);
                std::string optionsLine;
                if (std::getline(optionsStream, optionsLine)) {
                    std::istringstream optionsLineStream(optionsLine);
                    std::string engine, collation, row_format;
                    std::getline(optionsLineStream, engine, '\t');
                    std::getline(optionsLineStream, collation, '\t');
                    std::getline(optionsLineStream, row_format);

                    // Store in entity options
                    if (!engine.empty()) entity.options["engine"] = engine;
                    if (!collation.empty()) entity.options["collation"] = collation;
                    if (!row_format.empty()) entity.options["row_format"] = row_format;
                }
            }

            dbEntities_[entity.name] = entity;
        }

        // Get view information using executeQuery
        std::string viewsQuery =
            "SELECT table_name, REPLACE(REPLACE(view_definition, CHAR(13), ' '), CHAR(10), ' ') "
            "FROM information_schema.views WHERE table_schema = DATABASE()";
        std::string viewResultStr = db->executeQuery(viewsQuery);

        // Parse view results
        std::vector<std::map<std::string, std::string>> viewResults;
        std::istringstream viewStream(viewResultStr);
        std::string viewLine;
        while (std::getline(viewStream, viewLine)) {
            std::map<std::string, std::string> row;
            std::istringstream viewLineStream(viewLine);
            std::string table_name, view_definition;
            std::getline(viewLineStream, table_name, '\t');
            std::getline(viewLineStream, view_definition);
            row["table_name"] = table_name;
            row["view_definition"] = view_definition;
            viewResults.push_back(row);
        }

        for (const auto& viewRow : viewResults) {
            ViewDefinition view;
            view.viewName = viewRow.at("table_name");
            view.name = viewRow.at("table_name");
            view.query = viewRow.at("view_definition");
            view.isUpdatable = false;

            dbViews_.push_back(view);
        }

        // Get triggers
        std::string triggersQuery =
            "SELECT trigger_name, event_manipulation, action_timing, "
            "REPLACE(REPLACE(action_statement, CHAR(13), ' '), CHAR(10), ' '), "
            "REPLACE(REPLACE(COALESCE(action_condition, ''), CHAR(13), ' '), CHAR(10), ' '), "
            "event_object_table "
            "FROM information_schema.triggers "
            "WHERE event_object_schema = DATABASE()";
        std::string triggersResult = db->executeQuery(triggersQuery);

        // Parse triggers
        std::vector<std::map<std::string, std::string>> triggerResults;
        std::istringstream triggerStream(triggersResult);
        std::string triggerLine;
        while (std::getline(triggerStream, triggerLine)) {
            std::map<std::string, std::string> row;
            std::istringstream triggerLineStream(triggerLine);
            std::string trigger_name, event_manipulation, action_timing, action_statement, action_condition, event_object_table;
            std::getline(triggerLineStream, trigger_name, '\t');
            std::getline(triggerLineStream, event_manipulation, '\t');
            std::getline(triggerLineStream, action_timing, '\t');
            std::getline(triggerLineStream, action_statement, '\t');
            std::getline(triggerLineStream, action_condition, '\t');
            std::getline(triggerLineStream, event_object_table);
            row["trigger_name"] = trigger_name;
            row["event_manipulation"] = event_manipulation;
            row["action_timing"] = action_timing;
            row["action_statement"] = action_statement;
            row["action_condition"] = action_condition;
            row["event_object_table"] = event_object_table;
            triggerResults.push_back(row);
        }

        for (const auto& triggerRow : triggerResults) {
            TriggerDefinition trigger;
            trigger.name = triggerRow.at("trigger_name");
            trigger.triggerName = triggerRow.at("trigger_name");
            trigger.tableName = triggerRow.at("event_object_table");
            trigger.event = triggerRow.at("event_manipulation");
            trigger.timing = triggerRow.at("action_timing");
            trigger.function = triggerRow.at("action_statement");
            trigger.condition = triggerRow.at("action_condition");
            trigger.enabled = true;
            dbTriggers_.push_back(trigger);
        }

        // Get stored procedures
        std::string proceduresQuery =
            "SELECT routine_name, REPLACE(REPLACE(routine_definition, CHAR(13), ' '), CHAR(10), ' '), data_type "
            "FROM information_schema.routines "
            "WHERE routine_type = 'PROCEDURE' "
            "AND routine_schema = DATABASE()";
        std::string proceduresResult = db->executeQuery(proceduresQuery);

        // Parse procedures
        std::vector<std::map<std::string, std::string>> procedureResults;
        std::istringstream procedureStream(proceduresResult);
        std::string procedureLine;
        while (std::getline(procedureStream, procedureLine)) {
            std::map<std::string, std::string> row;
            std::istringstream procedureLineStream(procedureLine);
            std::string routine_name, routine_definition, data_type;
            std::getline(procedureLineStream, routine_name, '\t');
            std::getline(procedureLineStream, routine_definition, '\t');
            std::getline(procedureLineStream, data_type);
            row["routine_name"] = routine_name;
            row["routine_definition"] = routine_definition;
            row["data_type"] = data_type;
            procedureResults.push_back(row);
        }

        for (const auto& procRow : procedureResults) {
            StoredProcedureDefinition sp;
            sp.name = procRow.at("routine_name");
            sp.procedureName = procRow.at("routine_name");
            sp.code = procRow.at("routine_definition");
            sp.returnType = procRow.at("data_type");
            dbStoredProcedures_.push_back(sp);
        }

        // Get functions
        std::string functionsQuery =
            "SELECT routine_name, REPLACE(REPLACE(routine_definition, CHAR(13), ' '), CHAR(10), ' '), data_type "
            "FROM information_schema.routines "
            "WHERE routine_type = 'FUNCTION' "
            "AND routine_schema = DATABASE()";
        std::string functionsResult = db->executeQuery(functionsQuery);

        // Parse functions
        std::vector<std::map<std::string, std::string>> functionResults;
        std::istringstream functionStream(functionsResult);
        std::string functionLine;
        while (std::getline(functionStream, functionLine)) {
            std::map<std::string, std::string> row;
            std::istringstream functionLineStream(functionLine);
            std::string routine_name, routine_definition, data_type;
            std::getline(functionLineStream, routine_name, '\t');
            std::getline(functionLineStream, routine_definition, '\t');
            std::getline(functionLineStream, data_type);
            row["routine_name"] = routine_name;
            row["routine_definition"] = routine_definition;
            row["data_type"] = data_type;
            functionResults.push_back(row);
        }

        for (const auto& funcRow : functionResults) {
            DatabaseFunctionDefinition dbFunc;
            dbFunc.name = funcRow.at("routine_name");
            dbFunc.functionName = funcRow.at("routine_name");
            dbFunc.code = funcRow.at("routine_definition");
            dbFunc.returnType = funcRow.at("data_type");
            dbDatabaseFunctions_.push_back(dbFunc);
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading schema from MySQL: " << e.what() << std::endl;
        return false;
    }
}

bool SchemaLoader::loadSchemaFromSQLite(std::shared_ptr<DatabaseInterface> db) {
    try {
        // Get table information using executeQuery
        std::string tablesQuery = "SELECT name FROM sqlite_master WHERE type = 'table'";
        std::string tablesResult = db->executeQuery(tablesQuery);

        // Parse table results
        std::vector<std::map<std::string, std::string>> tableResults;
        std::istringstream tablesStream(tablesResult);
        std::string line;
        while (std::getline(tablesStream, line)) {
            std::map<std::string, std::string> row;
            row["name"] = line;
            tableResults.push_back(row);
        }

        for (const auto& tableRow : tableResults) {
            std::string tableName = tableRow.at("name");

            // Skip SQLite system tables
            if (tableName == "sqlite_sequence") {
                continue;
            }

            EntityDefinition entity;
            entity.tableName = tableName;
            entity.name = tableName;

            // Get column information using executeQuery for PRAGMA queries
            std::string schemaQuery = "PRAGMA table_info('" + tableName + "')";
            std::string columnResult = db->executeQuery(schemaQuery);

            // Parse column results
            std::vector<std::map<std::string, std::string>> columnResults;
            std::istringstream columnStream(columnResult);
            std::string columnLine;
            while (std::getline(columnStream, columnLine)) {
                std::map<std::string, std::string> row;
                std::istringstream columnLineStream(columnLine);
                std::string cid, name, type, notnull, dflt_value, pk;
                std::getline(columnLineStream, cid, '\t');
                std::getline(columnLineStream, name, '\t');
                std::getline(columnLineStream, type, '\t');
                std::getline(columnLineStream, notnull, '\t');
                std::getline(columnLineStream, dflt_value, '\t');
                std::getline(columnLineStream, pk);
                row["cid"] = cid;
                row["name"] = name;
                row["type"] = type;
                row["notnull"] = notnull;
                row["dflt_value"] = dflt_value;
                row["pk"] = pk;
                columnResults.push_back(row);
            }

            for (const auto& columnRow : columnResults) {
                FieldDefinition field;
                field.name = columnRow.at("name");
                field.type = mapDBTypeToAppType(columnRow.at("type"), "sqlite");
                field.nullable = (columnRow.at("notnull") == "0");
                field.primaryKey = (columnRow.at("pk") != "0");

                // Set default value
                if (columnRow.find("dflt_value") != columnRow.end() && !columnRow.at("dflt_value").empty()) {
                    field.defaultValue = columnRow.at("dflt_value");
                }

                // Check for autoincrement
                std::string autoincQuery = "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = '" + tableName + "'";
                std::string autoincResult = db->executeQuery(autoincQuery);
                if (autoincResult.find("AUTOINCREMENT") != std::string::npos &&
                    autoincResult.find(field.name) != std::string::npos) {
                    field.autoIncrement = true;
                }

                entity.fields.push_back(field);
            }

            // Get foreign key information using executeQuery
            std::string fkQuery = "PRAGMA foreign_key_list('" + tableName + "')";
            std::string fkResult = db->executeQuery(fkQuery);

            // Parse FK results
            std::vector<std::map<std::string, std::string>> fkResults;
            std::istringstream fkStream(fkResult);
            std::string fkLine;
            while (std::getline(fkStream, fkLine)) {
                std::map<std::string, std::string> row;
                std::istringstream fkLineStream(fkLine);
                std::string id, seq, table, from, to, on_update, on_delete, match;
                std::getline(fkLineStream, id, '\t');
                std::getline(fkLineStream, seq, '\t');
                std::getline(fkLineStream, table, '\t');
                std::getline(fkLineStream, from, '\t');
                std::getline(fkLineStream, to, '\t');
                std::getline(fkLineStream, on_update, '\t');
                std::getline(fkLineStream, on_delete, '\t');
                std::getline(fkLineStream, match);
                row["id"] = id;
                row["seq"] = seq;
                row["from"] = from;
                row["to"] = to;
                row["table"] = table;
                row["on_update"] = on_update;
                row["on_delete"] = on_delete;
                row["match"] = match;
                fkResults.push_back(row);
            }

            for (const auto& fkRow : fkResults) {
                ForeignKeyField fkField;
                fkField.name = fkRow.at("from");
                fkField.references = fkRow.at("table");
                fkField.toField = fkRow.at("to").empty() ? "id" : fkRow.at("to");
                fkField.onUpdate = fkRow.at("on_update");
                fkField.onDelete = fkRow.at("on_delete");
                fkField.type = "FOREIGN_KEY";

                entity.foreignKeys.push_back(fkField);
            }

            // Get index information using executeQuery
            std::string indexQuery = "PRAGMA index_list('" + tableName + "')";
            std::string indexResult = db->executeQuery(indexQuery);

            // Parse index results
            std::vector<std::map<std::string, std::string>> indexResults;
            std::istringstream indexStream(indexResult);
            std::string indexLine;
            while (std::getline(indexStream, indexLine)) {
                std::map<std::string, std::string> row;
                std::istringstream indexLineStream(indexLine);
                std::string seq, name, unique, origin, partial;
                std::getline(indexLineStream, seq, '\t');
                std::getline(indexLineStream, name, '\t');
                std::getline(indexLineStream, unique, '\t');
                std::getline(indexLineStream, origin, '\t');
                std::getline(indexLineStream, partial);
                row["seq"] = seq;
                row["name"] = name;
                row["unique"] = unique;
                row["origin"] = origin;
                row["partial"] = partial;
                indexResults.push_back(row);
            }

            for (const auto& indexRow : indexResults) {
                IndexDefinition index;
                index.name = indexRow.at("name");
                index.unique = (indexRow.at("unique") != "0");

                // Get index field information
                std::string indexInfoQuery = "PRAGMA index_info('" + index.name + "')";
                std::string indexInfoResult = db->executeQuery(indexInfoQuery);

                // Parse index info results
                std::vector<std::map<std::string, std::string>> indexInfoResults;
                std::istringstream indexInfoStream(indexInfoResult);
                std::string indexInfoLine;
                while (std::getline(indexInfoStream, indexInfoLine)) {
                    std::map<std::string, std::string> row;
                    std::istringstream indexInfoLineStream(indexInfoLine);
                    std::string seqno, cid, name;
                    std::getline(indexInfoLineStream, seqno, '\t');
                    std::getline(indexInfoLineStream, cid, '\t');
                    std::getline(indexInfoLineStream, name);
                    row["seqno"] = seqno;
                    row["cid"] = cid;
                    row["name"] = name;
                    indexInfoResults.push_back(row);
                }

                for (const auto& indexInfoRow : indexInfoResults) {
                    index.fieldNames.push_back(indexInfoRow.at("name"));
                }

                entity.indexes.push_back(index);
            }

            // Get table constraints (from CREATE TABLE statement)
            std::string tableSqlQuery = "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = '" + tableName + "'";
            std::string tableSqlResult = db->executeQuery(tableSqlQuery);

            if (!tableSqlResult.empty()) {
                // Extract CHECK constraints from CREATE TABLE statement
                size_t checkPos = tableSqlResult.find("CHECK");
                while (checkPos != std::string::npos) {
                    size_t openParen = tableSqlResult.find('(', checkPos);
                    if (openParen != std::string::npos) {
                        int parenCount = 1;
                        size_t i = openParen + 1;
                        while (i < tableSqlResult.length() && parenCount > 0) {
                            if (tableSqlResult[i] == '(') parenCount++;
                            else if (tableSqlResult[i] == ')') parenCount--;
                            i++;
                        }

                        if (parenCount == 0) {
                            std::string checkExpression = tableSqlResult.substr(openParen, i - openParen);
                            CheckConstraintDefinition checkConstraint;
                            checkConstraint.name = "check_" + tableName + "_" + std::to_string(checkPos);
                            checkConstraint.expression = checkExpression;
                            entity.checkConstraints.push_back(checkConstraint);
                        }
                    }
                    checkPos = tableSqlResult.find("CHECK", checkPos + 1);
                }
            }

            dbEntities_[entity.name] = entity;
        }

        // Get view information using executeQuery
        std::string viewsQuery =
            "SELECT name, replace(replace(sql, char(13), ' '), char(10), ' ') "
            "FROM sqlite_master WHERE type = 'view'";
        std::string viewResult = db->executeQuery(viewsQuery);

        // Parse view results
        std::vector<std::map<std::string, std::string>> viewResults;
        std::istringstream viewStream(viewResult);
        std::string viewLine;
        while (std::getline(viewStream, viewLine)) {
            std::map<std::string, std::string> row;
            std::istringstream viewLineStream(viewLine);
            std::string name, sql;
            std::getline(viewLineStream, name, '\t');
            std::getline(viewLineStream, sql);
            row["name"] = name;
            row["sql"] = sql;
            viewResults.push_back(row);
        }

        for (const auto& viewRow : viewResults) {
            ViewDefinition view;
            view.viewName = viewRow.at("name");
            view.name = viewRow.at("name");
            view.query = viewRow.at("sql");
            view.isUpdatable = false;

            dbViews_.push_back(view);
        }

        // Get triggers
        std::string triggersQuery =
            "SELECT name, replace(replace(sql, char(13), ' '), char(10), ' '), tbl_name "
            "FROM sqlite_master WHERE type = 'trigger'";
        std::string triggersResult = db->executeQuery(triggersQuery);

        // Parse triggers
        std::vector<std::map<std::string, std::string>> triggerResults;
        std::istringstream triggerStream(triggersResult);
        std::string triggerLine;
        while (std::getline(triggerStream, triggerLine)) {
            std::map<std::string, std::string> row;
            std::istringstream triggerLineStream(triggerLine);
            std::string name, sql, tbl_name;
            std::getline(triggerLineStream, name, '\t');
            std::getline(triggerLineStream, sql, '\t');
            std::getline(triggerLineStream, tbl_name);
            row["name"] = name;
            row["sql"] = sql;
            row["tbl_name"] = tbl_name;
            triggerResults.push_back(row);
        }

        for (const auto& triggerRow : triggerResults) {
            TriggerDefinition trigger;
            trigger.name = triggerRow.at("name");
            trigger.triggerName = triggerRow.at("name");
            trigger.tableName = triggerRow.at("tbl_name");

            // Parse event and timing from SQL
            std::string sql = triggerRow.at("sql");
            if (sql.find("BEFORE") != std::string::npos) trigger.timing = "BEFORE";
            else if (sql.find("AFTER") != std::string::npos) trigger.timing = "AFTER";
            else if (sql.find("INSTEAD OF") != std::string::npos) trigger.timing = "INSTEAD_OF";

            if (sql.find("INSERT") != std::string::npos) trigger.event = "INSERT";
            else if (sql.find("UPDATE") != std::string::npos) trigger.event = "UPDATE";
            else if (sql.find("DELETE") != std::string::npos) trigger.event = "DELETE";

            trigger.function = sql; // Full SQL for now
            trigger.enabled = true;
            dbTriggers_.push_back(trigger);
        }

        // Get indexes information (already collected above, but we can add more details)
        // In SQLite, indexes are already handled in the table processing section

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading schema from SQLite: " << e.what() << std::endl;
        return false;
    }
}

std::string SchemaLoader::mapDBTypeToAppType(const std::string& dbType, const std::string& dbEngine) {
    std::string lowerType = dbType;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);

    if (dbEngine == "postgresql") {
        // Exact matches first
        if (lowerType == "integer") return "INTEGER";
        if (lowerType == "bigint") return "BIGINT";
        if (lowerType == "smallint") return "SMALLINT";
        if (lowerType == "serial") return "SERIAL";
        if (lowerType == "bigserial") return "BIGSERIAL";

        if (lowerType.find("character varying") != std::string::npos ||
            lowerType.find("varchar") != std::string::npos) return "VARCHAR";

        if (lowerType == "text") return "TEXT";
        if (lowerType == "char") return "CHAR";

        if (lowerType == "boolean") return "BOOLEAN";

        if (lowerType == "timestamp" ||
            lowerType == "timestamp without time zone" ||
            lowerType == "timestamp with time zone") return "TIMESTAMP";

        if (lowerType == "date") return "DATE";
        if (lowerType == "time" ||
            lowerType == "time without time zone" ||
            lowerType == "time with time zone") return "TIME";

        if (lowerType == "numeric" ||
            lowerType == "decimal") return "DECIMAL";

        if (lowerType == "real") return "REAL";
        if (lowerType == "double precision") return "DOUBLE";

        if (lowerType == "json") return "JSON";
        if (lowerType == "jsonb") return "JSONB";

        if (lowerType == "uuid") return "UUID";

        if (lowerType == "bytea") return "BYTEA";

        if (lowerType == "inet") return "INET";
        if (lowerType == "cidr") return "CIDR";
        if (lowerType == "macaddr") return "MACADDR";
    }
    else if (dbEngine == "mysql") {
        if (lowerType == "int" || lowerType == "integer") return "INTEGER";
        if (lowerType == "bigint") return "BIGINT";
        if (lowerType == "smallint") return "SMALLINT";
        if (lowerType == "mediumint") return "MEDIUMINT";
        if (lowerType == "tinyint") return "TINYINT";

        if (lowerType.find("varchar") != std::string::npos) return "VARCHAR";
        if (lowerType == "text") return "TEXT";
        if (lowerType == "tinytext") return "TINYTEXT";
        if (lowerType == "mediumtext") return "MEDIUMTEXT";
        if (lowerType == "longtext") return "LONGTEXT";
        if (lowerType == "char") return "CHAR";

        if (lowerType == "tinyint(1)") return "BOOLEAN";
        if (lowerType.find("bool") != std::string::npos) return "BOOLEAN";

        if (lowerType.find("timestamp") != std::string::npos) return "TIMESTAMP";
        if (lowerType == "date") return "DATE";
        if (lowerType == "time") return "TIME";
        if (lowerType == "datetime") return "DATETIME";
        if (lowerType == "year") return "YEAR";

        if (lowerType.find("decimal") != std::string::npos) return "DECIMAL";
        if (lowerType == "float") return "FLOAT";
        if (lowerType == "double") return "DOUBLE";

        if (lowerType == "json") return "JSON";

        if (lowerType.find("blob") != std::string::npos) return "BLOB";
        if (lowerType == "tinyblob") return "TINYBLOB";
        if (lowerType == "mediumblob") return "MEDIUMBLOB";
        if (lowerType == "longblob") return "LONGBLOB";
    }
    else if (dbEngine == "sqlite") {
        // SQLite is dynamically typed, but PRAGMA table_info returns the declared type.
        // Map common declared types to application types.
        if (lowerType == "integer" ||
            lowerType == "int" ||
            lowerType == "int4" ||
            lowerType == "int8" ||
            lowerType == "smallint" ||
            lowerType == "bigint" ||
            lowerType == "serial" ||
            lowerType == "bigserial" ||
            lowerType == "tinyint" ||
            lowerType == "mediumint") {
            return "INTEGER";
        }
        if (lowerType == "text" ||
            lowerType == "varchar" ||
            lowerType == "char" ||
            lowerType == "clob") {
            return "TEXT";
        }
        if (lowerType == "real" ||
            lowerType == "float" ||
            lowerType == "double" ||
            lowerType == "double precision" ||
            lowerType == "numeric" ||
            lowerType == "decimal") {
            return "REAL";
        }
        if (lowerType == "blob") {
            return "BLOB";
        }
        if (lowerType == "boolean" ||
            lowerType == "bool" ||
            lowerType == "tinyint(1)") {
            return "BOOLEAN";
        }
        if (lowerType == "timestamp" ||
            lowerType == "timestamp without time zone" ||
            lowerType == "timestamp with time zone" ||
            lowerType == "datetime") {
            return "TIMESTAMP";
        }
        if (lowerType == "date") {
            return "DATE";
        }
        if (lowerType == "time" ||
            lowerType == "time without time zone" ||
            lowerType == "time with time zone") {
            return "TIME";
        }
        if (lowerType == "json") {
            return "JSON";
        }
        if (lowerType == "uuid") {
            return "UUID";
        }
    }

    // Default fallback - return TEXT for SQLite (since it's dynamically typed)
    if (dbEngine == "sqlite") {
        return "TEXT";
    }
    return "VARCHAR";
}

bool SchemaLoader::loadSchemaFromFile(const std::string& schemaFile) {
    try {
        XmlSchemaValidator validator;
        const auto validation = validator.validateFile(schemaFile);
        if (!validation.ok()) {
            std::cerr << "Schema XML validation failed for: " << schemaFile << std::endl;
            std::cerr << "XSD contract: " << validation.schemaPath << std::endl;
            for (const auto& error : validation.errors) {
                std::cerr << "  [" << error.code << "]";
                if (error.line > 0) {
                    std::cerr << " line " << error.line;
                }
                if (!error.elementPath.empty()) {
                    std::cerr << " path " << error.elementPath;
                }
                std::cerr << ": " << error.message << std::endl;
            }
            return false;
        }

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(schemaFile.c_str());

        if (!result) {
            std::cerr << "Failed to load schema file: " << schemaFile
                      << " (Error: " << result.description() << ")" << std::endl;
            return false;
        }

        // Clear existing data
        entities_.clear();
        views_.clear();
        materializedViews_.clear();
        storedProcedures_.clear();
        triggers_.clear();
        databaseFunctions_.clear();
        sequences_.clear();
        databaseMapping_.entityMappings.clear();
        databaseMapping_.typeMapping.typeMaps.clear();

        // Parse the Application element
        pugi::xml_node appNode = doc.child("Application");
        if (!appNode) {
            std::cerr << "Missing Application root element in schema" << std::endl;
            return false;
        }

        // Load schema metadata
        schemaMetadata_.name = appNode.child("Name").text().as_string("");
        schemaMetadata_.version = appNode.child("Version").text().as_string("1.0.0");
        schemaMetadata_.description = appNode.child("Description").text().as_string("");
        schemaMetadata_.author = appNode.child("Author").text().as_string("");

        // Set creation time
        // Only set creation time if it's not already set
        if (schemaMetadata_.createdAt.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            schemaMetadata_.createdAt = ss.str();
        }

        pugi::xml_node configNode = appNode.child("Configuration");
        if (configNode) {
            configuration_.dbConnectionString = configNode.child("DbConnectionString").text().as_string("");
            configuration_.logLevel = configNode.child("LogLevel").text().as_string("INFO");
            configuration_.timeout = configNode.child("Timeout").text().as_int(30);
            configuration_.databaseEngine = configNode.child("DatabaseEngine").text().as_string("");

            // Save in metadata
            schemaMetadata_.configuration = configuration_;
        }

        // Parse DataStructure
        pugi::xml_node dataStructureNode = appNode.child("DataStructure");
        if (dataStructureNode) {
            // Parse Entities
            for (pugi::xml_node entityNode : dataStructureNode.children("Entity")) {
                EntityDefinition entity;
                entity.name = entityNode.attribute("tableName").value();
                entity.tableName = entityNode.attribute("tableName").value();
                entity.verboseName = entityNode.attribute("verboseName").value();
                entity.verboseNamePlural = entityNode.attribute("verboseNamePlural").value();
                entity.description = entityNode.attribute("description").value();
                entity.tablespace = entityNode.attribute("tablespace").value();
                entity.isPartitioned = entityNode.attribute("isPartitioned").as_bool(false);

                // Parse Fields
                for (pugi::xml_node fieldNode : entityNode.children("Field")) {
                    FieldDefinition field;
                    field.name = fieldNode.attribute("name").value();
                    field.type = fieldNode.attribute("type").value();
                    field.nullable = fieldNode.attribute("nullable").as_bool(false);
                    field.primaryKey = fieldNode.attribute("primaryKey").as_bool(false);
                    field.unique = fieldNode.attribute("unique").as_bool(false);
                    field.maxLength = fieldNode.attribute("maxLength").value();
                    field.defaultValue = fieldNode.attribute("defaultValue").value();
                    field.verboseName = fieldNode.attribute("verboseName").value();
                    field.description = fieldNode.attribute("description").value();
                    field.autoIncrement = fieldNode.attribute("autoIncrement").as_bool(false);
                    field.collation = fieldNode.attribute("collation").value();
                    field.computedExpression = fieldNode.attribute("computedExpression").value();
                    field.isComputed = fieldNode.attribute("isComputed").as_bool(false);
                    field.precision = fieldNode.attribute("precision").value();
                    field.scale = fieldNode.attribute("scale").value();
                    pugi::xml_node enumValuesNode = fieldNode.child("EnumValues");
                    if (enumValuesNode) {
                        for (pugi::xml_node valueNode : enumValuesNode.children("Value")) {
                            field.enumValues.push_back(valueNode.text().as_string());
                        }
                    }
                    entity.fields.push_back(field);
                }

                // Parse ForeignKeyFields
                for (pugi::xml_node fkNode : entityNode.children("ForeignKeyField")) {
                    ForeignKeyField fkField;
                    fkField.name = fkNode.attribute("name").value();
                    fkField.type = fkNode.attribute("type").value();
                    fkField.nullable = fkNode.attribute("nullable").as_bool(false);
                    fkField.primaryKey = fkNode.attribute("primaryKey").as_bool(false);
                    fkField.unique = fkNode.attribute("unique").as_bool(false);
                    fkField.maxLength = fkNode.attribute("maxLength").value();
                    fkField.defaultValue = fkNode.attribute("defaultValue").value();
                    fkField.verboseName = fkNode.attribute("verboseName").value();
                    fkField.description = fkNode.attribute("description").value();
                    fkField.autoIncrement = fkNode.attribute("autoIncrement").as_bool(false);
                    fkField.collation = fkNode.attribute("collation").value();
                    fkField.computedExpression = fkNode.attribute("computedExpression").value();
                    fkField.isComputed = fkNode.attribute("isComputed").as_bool(false);
                    fkField.precision = fkNode.attribute("precision").value();
                    fkField.scale = fkNode.attribute("scale").value();
                    fkField.references = fkNode.attribute("references").value();
                    fkField.onDelete = fkNode.attribute("onDelete").value();
                    fkField.onUpdate = fkNode.attribute("onUpdate").value();
                    fkField.toField = fkNode.attribute("toField").value();
                    fkField.relatedName = fkNode.attribute("relatedName").value();
                    pugi::xml_node enumValuesNode = fkNode.child("EnumValues");
                    if (enumValuesNode) {
                        for (pugi::xml_node valueNode : enumValuesNode.children("Value")) {
                            fkField.enumValues.push_back(valueNode.text().as_string());
                        }
                    }
                    entity.foreignKeys.push_back(fkField);

                    // Auto-create field for ForeignKeyField if it doesn't exist
                    bool fieldExists = false;
                    for (const auto& existingField : entity.fields) {
                        if (existingField.name == fkField.name) {
                            fieldExists = true;
                            break;
                        }
                    }

                    if (!fieldExists) {
                        FieldDefinition autoField;
                        autoField.name = fkField.name;
                        autoField.type = "INTEGER";  // Default type for foreign keys
                        autoField.nullable = fkField.nullable;
                        autoField.primaryKey = fkField.primaryKey;
                        autoField.unique = fkField.unique;
                        autoField.defaultValue = fkField.defaultValue;
                        autoField.verboseName = fkField.verboseName;
                        autoField.description = fkField.description;
                        autoField.references = fkField.references;

                        entity.fields.push_back(autoField);
                    }

                }

                // Parse Indexes
                for (pugi::xml_node indexNode : entityNode.children("Index")) {
                    IndexDefinition index;
                    index.name = indexNode.attribute("name").value();
                    index.unique = indexNode.attribute("unique").as_bool(false);
                    index.type = indexNode.attribute("type").value();
                    index.concurrently = indexNode.attribute("concurrently").as_bool(false);
                    index.tablespace = indexNode.attribute("tablespace").value();

                    for (pugi::xml_node fieldNameNode : indexNode.children("FieldName")) {
                        index.fieldNames.push_back(fieldNameNode.text().as_string());
                    }
                    entity.indexes.push_back(index);
                }

                // Parse Constraints
                for (pugi::xml_node constraintNode : entityNode.children("Constraint")) {
                    ConstraintDefinition constraint;
                    constraint.name = constraintNode.child("Name").text().as_string();
                    constraint.constraintName = constraintNode.attribute("constraintName").value();
                    constraint.type = constraintNode.attribute("type").value();
                    constraint.description = constraintNode.child("Description").text().as_string();
                    constraint.expression = constraintNode.child("Expression").text().as_string();
                    entity.constraints.push_back(constraint);
                }

                // Parse EntityFunctions
                pugi::xml_node functionsNode = entityNode.child("EntityFunctions");
                if (functionsNode) {
                    for (pugi::xml_node functionNode : functionsNode.children("Function")) {
                        FunctionDefinition func;
                        func.name = functionNode.child("Name").text().as_string();
                        func.description = functionNode.child("Description").text().as_string();
                        func.fileName = functionNode.attribute("fileName").value();
                        func.language = functionNode.attribute("language").value();
                        func.returnType = functionNode.child("ReturnType").text().as_string();
                        func.code = functionNode.child("Code").text().as_string();

                        // Parse parameters
                        pugi::xml_node paramsNode = functionNode.child("Parameters");
                        if (paramsNode) {
                            for (pugi::xml_node paramNode : paramsNode.children("Parameter")) {
                                ParameterDefinition param;
                                param.name = paramNode.attribute("name").value();
                                param.type = paramNode.attribute("type").value();
                                param.required = paramNode.attribute("required").as_bool(false);
                                param.mode = paramNode.attribute("mode").value();
                                func.parameters.push_back(param);
                            }
                        }

                        entity.functions.push_back(func);
                    }
                }

                pugi::xml_node tablespaceNode = entityNode.child("Tablespace");
                if (tablespaceNode && entity.tablespace.empty()) {
                    entity.tablespace = tablespaceNode.text().as_string();
                }

                pugi::xml_node optionsNode = entityNode.child("Options");
                if (optionsNode) {
                    for (pugi::xml_node optionNode : optionsNode.children("Option")) {
                        const std::string key = optionNode.attribute("key").value();
                        if (!key.empty()) {
                            entity.options[key] = optionNode.attribute("value").value();
                        }
                    }
                }

                pugi::xml_node partitionsNode = entityNode.child("Partitions");
                if (partitionsNode) {
                    PartitionDefinition partition;
                    partition.strategy = partitionsNode.child("Strategy").text().as_string();
                    pugi::xml_node columnsNode = partitionsNode.child("Columns");
                    if (columnsNode) {
                        for (pugi::xml_node columnNode : columnsNode.children("Column")) {
                            partition.columns.push_back(columnNode.text().as_string());
                        }
                    }
                    partition.expression = partitionsNode.child("Expression").text().as_string();

                    pugi::xml_node boundsNode = partitionsNode.child("Bounds");
                    if (boundsNode) {
                        for (pugi::xml_node boundNode : boundsNode.children("Bound")) {
                            PartitionBoundDefinition bound;
                            bound.partitionName = boundNode.child("PartitionName").text().as_string();
                            bound.lowerBound = boundNode.child("LowerBound").text().as_string();
                            bound.upperBound = boundNode.child("UpperBound").text().as_string();
                            pugi::xml_node valuesNode = boundNode.child("Values");
                            if (valuesNode) {
                                for (pugi::xml_node valueNode : valuesNode.children("Value")) {
                                    bound.values.push_back(valueNode.text().as_string());
                                }
                            }
                            partition.bounds.push_back(bound);
                        }
                    }

                    if (!partition.strategy.empty()) {
                        entity.partitions.push_back(partition);
                        entity.isPartitioned = true;
                    }
                }

                entities_[entity.name] = entity;
            }

            // Parse Views
            pugi::xml_node viewsNode = dataStructureNode.child("Views");
            if (viewsNode) {
                for (pugi::xml_node viewNode : viewsNode.children("View")) {
                    ViewDefinition view;
                    view.name = viewNode.child("Name").text().as_string();
                    view.viewName = viewNode.attribute("viewName").value();
                    view.description = viewNode.child("Description").text().as_string();
                    view.query = viewNode.child("Query").text().as_string();
                    view.isUpdatable = viewNode.attribute("isUpdatable").as_bool(false);

                    // Parse nested Field elements
                    pugi::xml_node fieldsNode = viewNode.child("Fields");
                    if (fieldsNode) {
                        // Here can add handling fields views, if this necessary
                        // For this will require create corresponding structure data
                    }
//                    std::cout  << "DEBUG: loaded view: " << view.name << " - " << view.query << std::endl;
                    views_.push_back(view);
                }
            }

            // Parse MaterializedViews
            pugi::xml_node matViewsNode = dataStructureNode.child("MaterializedViews");
            if (matViewsNode) {
                for (pugi::xml_node matViewNode : matViewsNode.children("MaterializedView")) {
                    MaterializedViewDefinition matView;
                    matView.name = matViewNode.child("Name").text().as_string();
                    matView.viewName = matViewNode.attribute("viewName").value();
                    matView.description = matViewNode.child("Description").text().as_string();
                    matView.query = matViewNode.child("Query").text().as_string();
                    matView.refreshStrategy = matViewNode.child("RefreshStrategy").text().as_string();
                    matView.withData = matViewNode.attribute("withData").as_bool(true);

                    // Parse nested Field elements
                    pugi::xml_node fieldsNode = matViewNode.child("Fields");
                    if (fieldsNode) {
                        // Here can add handling fields materialized views
                        // For this will require create corresponding structure data
                    }

                    materializedViews_.push_back(matView);
                }
            }

            // Parse StoredProcedures
            pugi::xml_node spNode = dataStructureNode.child("StoredProcedures");
            if (spNode) {
                for (pugi::xml_node procedureNode : spNode.children("StoredProcedure")) {
                    StoredProcedureDefinition sp;
                    sp.name = procedureNode.child("Name").text().as_string();
                    sp.procedureName = procedureNode.attribute("procedureName").value();
                    sp.description = procedureNode.child("Description").text().as_string();
                    sp.language = procedureNode.attribute("language").value();
                    sp.security = procedureNode.attribute("security").value();
                    sp.returnType = procedureNode.child("ReturnType").text().as_string();
                    sp.code = procedureNode.child("Code").text().as_string();

                    // Handle parameters with considering attribute mode
                    pugi::xml_node paramsNode = procedureNode.child("Parameters");
                    if (paramsNode) {
                        // For handling parameters with mode will require extend structure ParameterDefinition
                        // or create separate structure for parameters stored procedures
                    }

                    storedProcedures_.push_back(sp);
                }
            }

            // Parse Triggers
            pugi::xml_node triggersNode = dataStructureNode.child("Triggers");
            if (triggersNode) {
                for (pugi::xml_node triggerNode : triggersNode.children("Trigger")) {
                    TriggerDefinition trigger;
                    trigger.name = triggerNode.child("Name").text().as_string();
                    trigger.triggerName = triggerNode.attribute("triggerName").value();
                    trigger.tableName = triggerNode.attribute("tableName").value();
                    trigger.description = triggerNode.child("Description").text().as_string();
                    trigger.event = triggerNode.child("Event").text().as_string();
                    trigger.timing = triggerNode.child("Timing").text().as_string();
                    trigger.function = triggerNode.child("Function").text().as_string();
                    trigger.condition = triggerNode.child("Condition").text().as_string();
                    trigger.enabled = triggerNode.attribute("enabled").as_bool(true);
                    triggers_.push_back(trigger);
                }
            }

            // Parse DatabaseFunctions
            pugi::xml_node dbFunctionsNode = dataStructureNode.child("DatabaseFunctions");
            if (dbFunctionsNode) {
                for (pugi::xml_node functionNode : dbFunctionsNode.children("DatabaseFunction")) {
                    DatabaseFunctionDefinition dbFunc;
                    dbFunc.name = functionNode.child("Name").text().as_string();
                    dbFunc.functionName = functionNode.attribute("functionName").value();
                    dbFunc.description = functionNode.child("Description").text().as_string();
                    dbFunc.language = functionNode.attribute("language").value();
                    dbFunc.volatileType = functionNode.attribute("volatile").value();
                    dbFunc.security = functionNode.attribute("security").value();
                    dbFunc.returnType = functionNode.child("ReturnType").text().as_string();
                    dbFunc.code = functionNode.child("Code").text().as_string();
                    databaseFunctions_.push_back(dbFunc);
                }
            }

            // Parse Sequences
            pugi::xml_node sequencesNode = dataStructureNode.child("Sequences");
            if (sequencesNode) {
                for (pugi::xml_node sequenceNode : sequencesNode.children("Sequence")) {
                    SequenceDefinition sequence;
                    sequence.name = sequenceNode.attribute("name").value();
                    sequence.tableName = sequenceNode.attribute("tableName").value();
                    sequence.columnName = sequenceNode.attribute("columnName").value();
                    sequence.startValue = sequenceNode.attribute("startValue").as_llong(1);
                    sequence.increment = sequenceNode.attribute("increment").as_llong(1);
                    sequence.minValue = sequenceNode.attribute("minValue").as_llong(1);
                    sequence.maxValue = sequenceNode.attribute("maxValue").as_llong(LONG_LONG_MAX);
                    sequence.cycle = sequenceNode.attribute("cycle").as_bool(false);
                    sequence.cache = sequenceNode.attribute("cache").as_llong(1);
                    sequences_.push_back(sequence);
                }
            }
        }

        pugi::xml_node databaseMappingNode = appNode.child("DatabaseMapping");
        if (databaseMappingNode) {
            // Parse EntityMapping
            for (pugi::xml_node entityMappingNode : databaseMappingNode.children("EntityMapping")) {
                EntityMappingDefinition entityMapping;
                entityMapping.entityName = entityMappingNode.attribute("entityName").value();
                entityMapping.tableName = entityMappingNode.attribute("tableName").value();

                // Parse FieldMapping
                for (pugi::xml_node fieldMappingNode : entityMappingNode.children("FieldMapping")) {
                    FieldMappingDefinition fieldMapping;
                    fieldMapping.fieldName = fieldMappingNode.attribute("fieldName").value();
                    fieldMapping.columnName = fieldMappingNode.attribute("columnName").value();
                    fieldMapping.postgresqlType = fieldMappingNode.attribute("postgresqlType").value();
                    fieldMapping.mysqlType = fieldMappingNode.attribute("mysqlType").value();
                    fieldMapping.sqliteType = fieldMappingNode.attribute("sqliteType").value();
                    entityMapping.fieldMappings.push_back(fieldMapping);
                }

                databaseMapping_.entityMappings.push_back(entityMapping);
            }

            // Parse TypeMapping
            pugi::xml_node typeMappingNode = databaseMappingNode.child("TypeMapping");
            if (typeMappingNode) {
                for (pugi::xml_node typeMapNode : typeMappingNode.children("TypeMap")) {
                    TypeMapDefinition typeMap;
                    typeMap.appType = typeMapNode.attribute("appType").value();
                    typeMap.postgresqlType = typeMapNode.attribute("postgresqlType").value();
                    typeMap.mysqlType = typeMapNode.attribute("mysqlType").value();
                    typeMap.sqliteType = typeMapNode.attribute("sqliteType").value();
                    databaseMapping_.typeMapping.typeMaps.push_back(typeMap);
                }
            }

            // Save in metadata
            schemaMetadata_.databaseMapping = databaseMapping_;
        }

        processReverseRelationships(entities_);

        // Update current snapshot
        // updateCurrentSnapshot();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading schema: " << e.what() << std::endl;
        return false;
    }
}

void SchemaLoader::processDataStructure() {
    // Process the loaded entity definitions
    std::cout << "Processing " << entities_.size() << " entities" << std::endl;

    for (const auto& pair : entities_) {
        const EntityDefinition& entity = pair.second;
        std::cout << "Entity: " << entity.name << " (Table: " << entity.tableName << ")" << std::endl;
        // Additional processing would happen here
    }
}

// Implementation methods for working with metadata schema
SchemaMetadata SchemaLoader::loadSchemaMetadata(const std::string& schemaFile) {
    SchemaMetadata metadata;

    try {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(schemaFile.c_str());

        if (!result) {
            std::cerr << "Failed to load schema file for metadata: " << schemaFile
                      << " (Error: " << result.description() << ")" << std::endl;
            return metadata;
        }

        pugi::xml_node appNode = doc.child("Application");
        if (!appNode) {
            std::cerr << "Missing Application root element in schema" << std::endl;
            return metadata;
        }

        // Load application metadata
        metadata.version = appNode.child("Version").text().as_string("1.0.0");
        if (metadata.version.empty()) {
            metadata.version = "1.0.0";
        }

        metadata.description = appNode.child("Description").text().as_string("");
        metadata.author = appNode.child("Author").text().as_string("");

        // Set creation time only if not already set
        if (metadata.createdAt.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            metadata.createdAt = ss.str();
        }

        // Calculate hash based on current snapshot (excluding time fields)
        SchemaSnapshot tempSnapshot;
        tempSnapshot.metadata = metadata;
        tempSnapshot.entities = entities_;
        tempSnapshot.views = views_;
        tempSnapshot.materializedViews = materializedViews_;
        tempSnapshot.storedProcedures = storedProcedures_;
        tempSnapshot.triggers = triggers_;
        tempSnapshot.databaseFunctions = databaseFunctions_;

        metadata.hash = calculateSchemaHash(tempSnapshot);

        return metadata;
    } catch (const std::exception& e) {
        std::cerr << "Error loading schema metadata: " << e.what() << std::endl;
        return metadata;
    }
}

SchemaSnapshot SchemaLoader::getCurrentSnapshot() {
    return currentSnapshot_;
}

SchemaSnapshot SchemaLoader::createCurrentSnapshot() {
    SchemaSnapshot snapshot;
    snapshot.metadata = schemaMetadata_;
    snapshot.entities = entities_;
    snapshot.views = views_;
    snapshot.materializedViews = materializedViews_;
    snapshot.storedProcedures = storedProcedures_;
    snapshot.triggers = triggers_;
    snapshot.databaseFunctions = databaseFunctions_;
    return snapshot;
}

std::string SchemaLoader::calculateSchemaHash(const SchemaSnapshot& snapshot) {
    std::ostringstream schemaContent;

    // Only include stable metadata in hash calculation (exclude createdAt)
    schemaContent << snapshot.metadata.version << snapshot.metadata.description
                  << snapshot.metadata.author;

    // Add entities information
    for (const auto& entityPair : snapshot.entities) {
        const EntityDefinition& entity = entityPair.second;
        schemaContent << entity.name << entity.tableName << entity.verboseName
                      << entity.verboseNamePlural << entity.description;

        // Add fields
        for (const auto& field : entity.fields) {
            schemaContent << field.name << field.type << field.nullable << field.primaryKey
                          << field.unique << field.maxLength << field.defaultValue
                          << field.verboseName << field.description;
        }

        // Add foreign keys
        for (const auto& fk : entity.foreignKeys) {
            schemaContent << fk.name << fk.type << fk.nullable << fk.primaryKey
                          << fk.unique << fk.maxLength << fk.defaultValue
                          << fk.verboseName << fk.description << fk.references
                          << fk.onDelete << fk.onUpdate << fk.toField;
        }

        // Add indexes
        for (const auto& index : entity.indexes) {
            schemaContent << index.name;
            for (const auto& fieldName : index.fieldNames) {
                schemaContent << fieldName;
            }
            schemaContent << index.unique << index.type << index.concurrently
                          << index.tablespace;
        }

        // Add constraints
        for (const auto& constraint : entity.constraints) {
            schemaContent << constraint.name << constraint.constraintName
                          << constraint.type << constraint.description
                          << constraint.expression;
        }
    }

    // Add views information
    for (const auto& view : snapshot.views) {
        schemaContent << view.name << view.viewName << view.description
                      << view.query << view.isUpdatable;
    }

    // Add materialized views information
    for (const auto& matView : snapshot.materializedViews) {
        schemaContent << matView.name << matView.viewName << matView.description
                      << matView.query << matView.refreshStrategy << matView.withData;
    }

    // Add stored procedures information
    for (const auto& proc : snapshot.storedProcedures) {
        schemaContent << proc.name << proc.procedureName << proc.description
                      << proc.language << proc.security << proc.returnType
                      << proc.code;
    }

    // Add triggers information
    for (const auto& trigger : snapshot.triggers) {
        schemaContent << trigger.name << trigger.triggerName << trigger.tableName
                      << trigger.description << trigger.event << trigger.timing
                      << trigger.function << trigger.condition << trigger.enabled;
    }

    // Add database functions information
    for (const auto& func : snapshot.databaseFunctions) {
        schemaContent << func.name << func.functionName << func.description
                      << func.language << func.volatileType << func.security
                      << func.returnType << func.code;
    }

    // Calculate hash from the assembled content
    std::hash<std::string> hasher;
    return std::to_string(hasher(schemaContent.str()));
}


const SchemaMetadata& SchemaLoader::getSchemaMetadata() {
    return schemaMetadata_;
}

bool SchemaLoader::saveSchemaMetadataToFile(const SchemaMetadata& metadata, const std::string& filename) {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open metadata file for writing: " << filename << std::endl;
            return false;
        }

        // Write metadata in format JSON
        file << "{\n";
        file << "  \"version\": \"" << metadata.version << "\",\n";
        file << "  \"created_at\": \"" << metadata.createdAt << "\",\n";
        file << "  \"description\": \"" << metadata.description << "\",\n";
        file << "  \"author\": \"" << metadata.author << "\",\n";
        file << "  \"hash\": \"" << metadata.hash << "\"\n";
        file << "}\n";

        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving schema metadata to file: " << e.what() << std::endl;
        return false;
    }
}

SchemaMetadata SchemaLoader::loadSchemaMetadataFromFile(const std::string& filename) {
    SchemaMetadata metadata;
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Metadata file does not exist: " << filename << std::endl;
            return metadata;
        }

        // Simple parser JSON (in real implementation better to use library JSON)
        std::string line;
        while (std::getline(file, line)) {
            // Simplified Parse JSON
            if (line.find("\"version\"") != std::string::npos) {
                size_t start = line.find(":\"") + 2;
                size_t end = line.find_last_of("\"");
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    metadata.version = line.substr(start, end - start);
                }
            } else if (line.find("\"created_at\"") != std::string::npos) {
                size_t start = line.find(":\"") + 2;
                size_t end = line.find_last_of("\"");
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    metadata.createdAt = line.substr(start, end - start);
                }
            } else if (line.find("\"description\"") != std::string::npos) {
                size_t start = line.find(":\"") + 2;
                size_t end = line.find_last_of("\"");
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    metadata.description = line.substr(start, end - start);
                }
            } else if (line.find("\"author\"") != std::string::npos) {
                size_t start = line.find(":\"") + 2;
                size_t end = line.find_last_of("\"");
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    metadata.author = line.substr(start, end - start);
                }
            } else if (line.find("\"hash\"") != std::string::npos) {
                size_t start = line.find(":\"") + 2;
                size_t end = line.find_last_of("\"");
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    metadata.hash = line.substr(start, end - start);
                }
            }
        }

        file.close();
    } catch (const std::exception& e) {
        std::cerr << "Error loading schema metadata from file: " << e.what() << std::endl;
    }
    return metadata;
}

// TODO: remove if unused
void SchemaLoader::setSchemaMetadata(const SchemaMetadata& metadata) {
    schemaMetadata_ = metadata;
    currentSnapshot_.metadata = metadata;
}

// TODO: remove if unused
bool SchemaLoader::isSchemaMetadataUpdateNeeded(const SchemaMetadata& currentMetadata) {
    // Check need update by various criteria
    if (schemaMetadata_.version != currentMetadata.version) {
        return true;
    }

    if (schemaMetadata_.description != currentMetadata.description) {
        return true;
    }

    if (schemaMetadata_.author != currentMetadata.author) {
        return true;
    }

    // Can add other criteria comparison
    return false;
}

std::string SchemaLoader::getCurrentSchemaVersion() {
    return schemaMetadata_.version;
}

std::vector<std::string> SchemaLoader::getSchemaVersionHistory() {
    std::vector<std::string> versions;

    try {
        // Get history versions from database (if available)
        if (EntityInterface::db) {
            try {
                auto results = EntityInterface::db->table("schema_metadata")
                              .order_by("created_at ASC")
                              .values({"version"})
                              .execute();

                for (const auto& row : results) {
                    if (row.find("version") != row.end()) {
                        versions.push_back(row.at("version"));
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not retrieve version history from database: " << e.what() << std::endl;
            }
        }

        // Add current version, if it no in list
        if (std::find(versions.begin(), versions.end(), schemaMetadata_.version) == versions.end()) {
            versions.push_back(schemaMetadata_.version);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error getting schema version history: " << e.what() << std::endl;
    }

    return versions;
}

SchemaComparisonResult SchemaLoader::compareSchemaEntities() {
    SchemaComparisonResult result;
    result.schemasMatch = true;

//    for (const auto &dbEntityPair : dbEntities_) {
//        const std::string& dbEntityName = dbEntityPair.first;
//        const EntityDefinition& dbEntity = dbEntityPair.second;
//        std::cout  << "DEBUG: Comparing dbEntity: " << dbEntityName << std::endl;
//    }

    // Compare entities in application schema with database entities
    for (const auto& appEntityPair : entities_) {
        const std::string& entityName = appEntityPair.first;
        const EntityDefinition& appEntity = appEntityPair.second;
        std::cout  << "DEBUG: Comparing entity: " << entityName << std::endl;

//        for (const auto &dbEntityPair : dbEntities_) {
//            const std::string& dbEntityName = dbEntityPair.first;
//            const EntityDefinition& dbEntity = dbEntityPair.second;
//
//            if (entityName == dbEntityName) {
//                // Entities match, compare fields
//                std::cout  << "DEBUG: Comparing fields for entity: " << entityName << std::endl;
//
//                for (const auto& appFieldPair : appEntity.fields) {
//                    const std::string& fieldName = appFieldPair.name;
//                }
//            }
//        }

        auto dbEntityIt = dbEntities_.find(entityName);
        // std::cout  << "DEBUG: Comparing dbEntity: " << dbEntityIt->first << std::endl;
        if (dbEntityIt == dbEntities_.end()) {
            // Entity exists in app schema but not in database
            result.missingEntities.push_back(entityName);
            result.schemasMatch = false;
        } else {
            // Entity exists in both, compare details
            const EntityDefinition& dbEntity = dbEntityIt->second;
            EntityDifferences differences;

            // Compare fields
            std::map<std::string, FieldDefinition> appFields;
            std::map<std::string, FieldDefinition> dbFields;

            for (const auto& field : appEntity.fields) {
                appFields[field.name] = field;
//                std::cout  << "DEBUG: Comparing appFields field: " << field.name << std::endl;
            }

            for (const auto& field : dbEntity.fields) {
                dbFields[field.name] = field;
//                std::cout  << "DEBUG: Comparing dbFields field: " << field.name << std::endl;
            }

            // Find missing fields (in app but not in DB)
            for (const auto& appFieldPair : appFields) {
                const std::string& fieldName = appFieldPair.first;
                const FieldDefinition& appField = appFieldPair.second;

                auto dbFieldIt = dbFields.find(fieldName);
                if (dbFieldIt == dbFields.end()) {
                    differences.missingFields.push_back(fieldName);
                    result.schemasMatch = false;
                } else {
                    // Compare field details
                    const FieldDefinition& dbField = dbFieldIt->second;

//                    std::cout << "DEBUG: Comparing field '" << fieldName << "'" << std::endl;
//                    std::cout << "  App field - Type: " << appField.type
//                              << ", Nullable: " << (appField.nullable ? "true" : "false")
//                              << ", PrimaryKey: " << (appField.primaryKey ? "true" : "false")
//                              << ", Unique: " << (appField.unique ? "true" : "false") << std::endl;
//                    std::cout << "  DB field  - Type: " << dbField.type
//                              << ", Nullable: " << (dbField.nullable ? "true" : "false")
//                              << ", PrimaryKey: " << (dbField.primaryKey ? "true" : "false")
//                              << ", Unique: " << (dbField.unique ? "true" : "false") << std::endl;

                    if (appField.type != dbField.type ||
                        appField.nullable != dbField.nullable ||
                        appField.primaryKey != dbField.primaryKey ||
                        appField.unique != dbField.unique) {
                        differences.fieldMismatches.push_back(fieldName);
                        result.schemasMatch = false;
                    }
                }
            }

            // Find extra fields (in DB but not in app)
            for (const auto& dbFieldPair : dbFields) {
                const std::string& fieldName = dbFieldPair.first;
                if (appFields.find(fieldName) == appFields.end()) {
                    differences.extraFields.push_back(fieldName);
                    result.schemasMatch = false;
                }
            }

            // Compare foreign keys
            std::map<std::string, ForeignKeyField> appForeignKeys;
            std::map<std::string, ForeignKeyField> dbForeignKeys;

            for (const auto& fk : appEntity.foreignKeys) {
                appForeignKeys[fk.name] = fk;
            }

            for (const auto& fk : dbEntity.foreignKeys) {
                dbForeignKeys[fk.name] = fk;
            }

            // Check for FK differences
            for (const auto& appFKPair : appForeignKeys) {
                const std::string& fkName = appFKPair.first;
                const ForeignKeyField& appFK = appFKPair.second;

                auto dbFKIt = dbForeignKeys.find(fkName);
                if (dbFKIt == dbForeignKeys.end()) {
                    differences.foreignKeyDifferences.push_back("Missing FK: " + fkName);
                    result.schemasMatch = false;
                } else {
                    const ForeignKeyField& dbFK = dbFKIt->second;
                    if (appFK.references != dbFK.references ||
                        appFK.toField != dbFK.toField ||
                        appFK.onDelete != dbFK.onDelete ||
                        appFK.onUpdate != dbFK.onUpdate) {
                        differences.foreignKeyDifferences.push_back("FK mismatch: " + fkName);
                        result.schemasMatch = false;
                    }
                }
            }

            // Check for extra FKs in DB
            for (const auto& dbFKPair : dbForeignKeys) {
                const std::string& fkName = dbFKPair.first;
                if (appForeignKeys.find(fkName) == appForeignKeys.end()) {
                    differences.foreignKeyDifferences.push_back("Extra FK: " + fkName);
                    result.schemasMatch = false;
                }
            }

            // Compare indexes
            // This is a simplified comparison - in practice, you might want to compare by name or fields
            if (appEntity.indexes.size() != dbEntity.indexes.size()) {
                differences.indexDifferences.push_back("Index count mismatch");
                result.schemasMatch = false;
            }

            // Compare constraints
            // This is a simplified comparison - in practice, you might want to compare by name or expression
            if (appEntity.constraints.size() != dbEntity.constraints.size()) {
                differences.constraintDifferences.push_back("Constraint count mismatch");
                result.schemasMatch = false;
            }

            // Add differences if any were found
            if (!differences.fieldMismatches.empty() ||
                !differences.missingFields.empty() ||
                !differences.extraFields.empty() ||
                !differences.foreignKeyDifferences.empty() ||
                !differences.indexDifferences.empty() ||
                !differences.constraintDifferences.empty()) {
                result.entityDifferences[entityName] = differences;
            }
        }
    }

    // Find entities in database but not in application schema
    for (const auto& dbEntityPair : dbEntities_) {
        const std::string& entityName = dbEntityPair.first;
        if (entities_.find(entityName) == entities_.end()) {
            result.extraEntities.push_back(entityName);
            result.schemasMatch = false;
        }
    }

    return result;
}

SchemaComparisonResult SchemaLoader::compareSchemaViews(SchemaComparisonResult result) {
    // Compare views in application schema with database views
//    std::cout  << "DEBUG: Comparing views..." << std::endl;
    for (const auto& appView : views_) {
        const std::string& viewName = appView.name;
//        std::cout  << "DEBUG: Comparing view: " << viewName << std::endl;

        // Find matching view in database views
        auto dbViewIt = std::find_if(dbViews_.begin(), dbViews_.end(),
                                    [&viewName](const ViewDefinition& dbView) {
                                        return dbView.name == viewName;
                                    });

        if (dbViewIt == dbViews_.end()) {
            // View exists in app schema but not in database
            result.missingViews.push_back(viewName);
            result.schemasMatch = false;
        } else {
            // View exists in both, compare details
            const ViewDefinition& dbView = *dbViewIt;
            ViewDifferences differences;

            // Compare view properties
            if (appView.query != dbView.query) {
                differences.propertyMismatches.push_back("query mismatch");
                result.schemasMatch = false;
            }

            if (appView.isUpdatable != dbView.isUpdatable) {
                differences.propertyMismatches.push_back("isUpdatable mismatch");
                result.schemasMatch = false;
            }

            // Add differences if any were found
            if (!differences.propertyMismatches.empty()) {
                result.viewDifferences[viewName] = differences;
            }
        }
    }

    // Find views in database but not in application schema
    for (const auto& dbView : dbViews_) {
        const std::string& viewName = dbView.name;

        // Check if view exists in application schema
        auto appViewIt = std::find_if(views_.begin(), views_.end(),
                                     [&viewName](const ViewDefinition& appView) {
                                         return appView.name == viewName;
                                     });

        if (appViewIt == views_.end()) {
            result.extraViews.push_back(viewName);
            result.schemasMatch = false;
        }
    }

    return result;
}

bool SchemaLoader::applySchemaChangesToDB(std::shared_ptr<DatabaseInterface> db) {
    try {
        std::cout << "Applying schema changes to database..." << std::endl;

        // Get the comparison result to understand what needs to be changed
        const SchemaComparisonResult comparison = compareSchemaEntities();

        // Handle missing entities (entities in app schema but not in database)
        for (const std::string& entityName : comparison.missingEntities) {
            const EntityDefinition& entity = entities_.at(entityName);
            std::cout  << "DEBUG: Missing table for entity: " << entityName << std::endl;
            if (!createDBTable(db, entity)) {
                std::cerr << "Failed to create table for entity: " << entityName << std::endl;
                return false;
            }
            std::cout << "Created table for entity: " << entityName << std::endl;
        }

        // Handle entity differences (entities that exist in both but have differences)
        for (const auto& pair : comparison.entityDifferences) {
            const std::string& entityName = pair.first;
            const EntityDifferences& differences = pair.second;

            const EntityDefinition& appEntity = entities_.at(entityName);
            auto dbEntityIt = dbEntities_.find(entityName);

            if (dbEntityIt != dbEntities_.end()) {
                const EntityDefinition& dbEntity = dbEntityIt->second;

                // Handle missing fields
                for (const std::string& fieldName : differences.missingFields) {
                    auto fieldIt = std::find_if(appEntity.fields.begin(), appEntity.fields.end(),
                                              [&fieldName](const FieldDefinition& f) {
                                                  return f.name == fieldName;
                                              });
                    if (fieldIt != appEntity.fields.end()) {
                        if (!addDBField(db, appEntity.tableName, *fieldIt)) {
                            std::cerr << "Failed to add field: " << fieldName
                                      << " to table: " << appEntity.tableName << std::endl;
                            return false;
                        }
                        std::cout << "Added field: " << fieldName
                                  << " to table: " << appEntity.tableName << std::endl;
                    }
                }

                // Handle field mismatches
                // Note: SQLite doesn't support ALTER COLUMN TYPE, so we skip field modifications for SQLite.
                // Field type changes in SQLite would require table recreation, which is not implemented here.
                std::string dbEngine;
                try {
                    dbEngine = getDbEngine();
                } catch (...) {
                    dbEngine = "";
                }
                bool skipFieldModification = (dbEngine == "sqlite");

                for (const std::string& fieldName : differences.fieldMismatches) {
                    auto appFieldIt = std::find_if(appEntity.fields.begin(), appEntity.fields.end(),
                                                  [&fieldName](const FieldDefinition& f) {
                                                      return f.name == fieldName;
                                                  });
                    if (appFieldIt != appEntity.fields.end()) {
                        if (skipFieldModification) {
                            // SQLite doesn't support modifying column types - skip silently
                            continue;
                        }
                        // Modify field (simplified - in practice this might require more complex handling)
                        if (!modifyDBField(db, appEntity.tableName, *appFieldIt)) {
                            std::cerr << "Failed to modify field: " << fieldName
                                      << " in table: " << appEntity.tableName << std::endl;
                            // Continue with other changes rather than failing completely
                        } else {
                            std::cout << "Modified field: " << fieldName
                                      << " in table: " << appEntity.tableName << std::endl;
                        }
                    }
                }

                // Handle foreign key differences
                for (const std::string& fkDiff : differences.foreignKeyDifferences) {
                    if (fkDiff.find("Missing FK:") == 0) {
                        std::string fkName = fkDiff.substr(12); // Remove "Missing FK: " prefix
                        auto fkIt = std::find_if(appEntity.foreignKeys.begin(), appEntity.foreignKeys.end(),
                                                [&fkName](const ForeignKeyField& fk) {
                                                    return fk.name == fkName;
                                                });
                        if (fkIt != appEntity.foreignKeys.end()) {
                            if (!addDBForeignKey(db, appEntity.tableName, *fkIt)) {
                                std::cerr << "Failed to add foreign key: " << fkName
                                          << " to table: " << appEntity.tableName << std::endl;
                                // Continue with other changes
                            } else {
                                std::cout << "Added foreign key: " << fkName
                                          << " to table: " << appEntity.tableName << std::endl;
                            }
                        }
                    }
                    // Note: Modifications to existing FKs would require dropping and recreating
                    // which is a more complex operation and might be handled separately
                }

                // Note: Index and constraint differences would be handled similarly
            }
        }

        // Check extra entities (exist in schema DB, but no in schema application)
        for (const std::string& extraEntityName : comparison.extraEntities) {
            // Remove table from database
            std::string dropQuery = "DROP TABLE IF EXISTS " + extraEntityName;
            db->executeNonQuery(dropQuery);
        }

        // Update dbEntities_ to reflect the current state
        for (const auto& pair : entities_) {
            const std::string& entityName = pair.first;
            const EntityDefinition& entity = pair.second;
            dbEntities_[entityName] = entity;
        }

        std::cout << "Schema changes applied successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error applying schema changes: " << e.what() << std::endl;
        return false;
    }
}

bool SchemaLoader::createDBTable(std::shared_ptr<DatabaseInterface> db,
                                 const EntityDefinition& entity) {
    try {
        // std::string dbEngine = configuration_.databaseEngine;
        std::string dbEngine = getDbEngine();
        std::cout  << "DEBUG: Using database engine: " << dbEngine << std::endl;
        std::cout  << "DEBUG: Creating table for entity: " << entity.name << std::endl;
        std::string sqlQuery = generateCreateTableSQL(entity, dbEngine);

        std::cout << "DEBUG: Generated SQL: " << sqlQuery << std::endl;

        if (!sqlQuery.empty()) {
            db->executeNonQuery(sqlQuery);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error creating table for entity " << entity.name << ": " << e.what() << std::endl;
        return false;
    }
}

std::string SchemaLoader::generateCreateTableSQL(const EntityDefinition& entity,
                                                   const std::string& dbEngine) {
    std::ostringstream sql;

    std::string tableName = entity.tableName;
    tableName = "\"" + tableName + "\"";

    sql << "CREATE TABLE " << tableName << " (";

    bool firstField = true;
    std::vector<std::string> primaryKeys;

    for (const auto& field : entity.fields) {
        if (!firstField) {
            sql << ", ";
        }

        sql << field.name << " ";

        bool primaryKeyHandledInline = false;

        // AUTO_INCREMENT / SERIAL
        if (field.autoIncrement) {
            if (dbEngine == "postgresql") {
                // For PostgreSQL usually is used SERIAL or IDENTITY
                sql << " SERIAL";
            } else if (dbEngine == "mysql") {
                sql << " INT AUTO_INCREMENT";
            } else if (dbEngine == "sqlite" || dbEngine == "sqlite3") {
                sql << " INTEGER";
                if (field.primaryKey) {
                    sql << " PRIMARY KEY AUTOINCREMENT";
                    primaryKeyHandledInline = true;
                }
            } else {
                sql << " INTEGER";
            }
        } else {
            // Map application type to database type
            std::cout << "DEBUG: Mapping app type: " << field.type << " dbEngine: " << dbEngine << std::endl;
            std::cout << "DEBUG: field.maxLength: " << field.maxLength << std::endl;
            std::string dbType = mapAppTypeToDBType(field.type, dbEngine, field.precision, field.scale,
                                                    field.maxLength);
            sql << dbType;
            std::cout << "DEBUG: Mapped app type: " << field.type << " to DB type: " << dbType << std::endl;
        }

        // NOT NULL
        if (!field.nullable && !primaryKeyHandledInline) {
            sql << " NOT NULL";
        }

        // DEFAULT
        if (!field.defaultValue.empty()) {
            sql << " DEFAULT " << field.defaultValue;
        }

        // UNIQUE
        if (field.unique) {
            sql << " UNIQUE";
        }

        // PRIMARY KEY (budet added after all fields)
        if (field.primaryKey && !primaryKeyHandledInline) {
            primaryKeys.push_back(field.name);
        }

        // COLLATION
        if (!field.collation.empty()) {
            sql << " COLLATE " << field.collation;
        }

        // COMPUTED COLUMNS
        if (field.isComputed && !field.computedExpression.empty()) {
            if (dbEngine == "postgresql") {
                sql << " GENERATED ALWAYS AS (" << field.computedExpression << ") STORED";
            } else if (dbEngine == "sqlserver") {
                sql << " AS (" << field.computedExpression << ") PERSISTED";
            }
        }

        firstField = false;
    }

    // Add composite primary key
    if (!primaryKeys.empty()) {
        sql << ", PRIMARY KEY (";
        for (size_t i = 0; i < primaryKeys.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << primaryKeys[i];
        }
        sql << ")";
    }

    // Add foreign key constraints
    for (const auto& fk : entity.foreignKeys) {
        sql << ", FOREIGN KEY (" << fk.name << ") REFERENCES "
            << fk.references << "(" << fk.toField << ")";

        if (!fk.onUpdate.empty()) {
            sql << " ON UPDATE " << fk.onUpdate;
        }

        if (!fk.onDelete.empty()) {
            sql << " ON DELETE " << fk.onDelete;
        }
    }

    sql << ")";

    return sql.str();
}

std::string SchemaLoader::mapAppTypeToDBType(const std::string& appType,
                                                const std::string& dbEngine,
                                                const std::string& precision,
                                                const std::string& scale,
                                                const std::string& maxLength) {

    // Default mappings if not found in configuration
    if (dbEngine == "postgresql") {
//        std::cout  << "DEBUG: Using default PostgreSQL type for app type: " << appType << std::endl;
        if (appType == "INTEGER") return "INTEGER";
        if (appType == "BIGINT") return "BIGINT";
        if (appType == "VARCHAR") {
            std::cout << "DEBUG: Using default PostgreSQL VARCHAR type for app type: " << maxLength << std::endl;
            if (!maxLength.empty()) {
                return "VARCHAR(" + maxLength + ")";
            }
            return "VARCHAR(255)";
        }
        if (appType == "TEXT") return "TEXT";
        if (appType == "DATE") return "DATE";
        if (appType == "TIMESTAMP") return "TIMESTAMP";
        if (appType == "BOOLEAN") return "BOOLEAN";
        if (appType == "DECIMAL") {
            std::string dbType = "DECIMAL";
            if (!scale.empty()) {
                dbType += "(" + precision + "," + scale + ")";
            } else {
                dbType += "(" + precision + ")";
            }
            return dbType;
        }
        if (appType == "FLOAT") return "FLOAT";
        if (appType == "DOUBLE") return "DOUBLE PRECISION";
        if (appType == "TIME") return "TIME";
        if (appType == "DATETIME") return "TIMESTAMP";
        if (appType == "JSON") return "JSON";
        if (appType == "JSONB") return "JSONB";
        if (appType == "UUID") return "UUID";
        if (appType == "BLOB") return "BYTEA";
        if (appType == "TINYINT") return "SMALLINT";
        if (appType == "SMALLINT") return "SMALLINT";
        if (appType == "MEDIUMINT") return "INTEGER";
        if (appType == "INT") return "INTEGER";
        if (appType == "REAL") return "REAL";
        if (appType == "CHAR") return "CHAR(1)";
        if (appType == "TINYTEXT") return "TEXT";
        if (appType == "MEDIUMTEXT") return "TEXT";
        if (appType == "LONGTEXT") return "TEXT";
        if (appType == "YEAR") return "INTEGER";
        if (appType == "ENUM") return "TEXT";  // PostgreSQL doesn't have native ENUM, using TEXT
        if (appType == "SET") return "TEXT";   // PostgreSQL doesn't have native SET, using TEXT
        if (appType == "TINYBLOB") return "BYTEA";
        if (appType == "MEDIUMBLOB") return "BYTEA";
        if (appType == "LONGBLOB") return "BYTEA";
        if (appType == "SERIAL") return "SERIAL";
        if (appType == "BIGSERIAL") return "BIGSERIAL";
        if (appType == "BYTEA") return "BYTEA";
        if (appType == "INET") return "INET";
        if (appType == "CIDR") return "CIDR";
        if (appType == "MACADDR") return "MACADDR";
        if (appType == "FOREIGN_KEY") return "INTEGER"; // Foreign keys are typically INTEGER
        if (appType == "VOID") return "VOID";
    } else if (dbEngine == "mysql") {
        if (appType == "INTEGER") return "INT";
        if (appType == "BIGINT") return "BIGINT";
        if (appType == "VARCHAR") {
            if (!maxLength.empty()) {
                return "VARCHAR(" + maxLength + ")";
            }
            return "VARCHAR(255)";
        }
        if (appType == "TEXT") return "TEXT";
        if (appType == "BOOLEAN") return "TINYINT(1)";
        if (appType == "TIMESTAMP") return "TIMESTAMP";
        if (appType == "DATE") return "DATE";
        if (appType == "DATETIME") return "DATETIME";
        if (appType == "DECIMAL" || appType == "NUMERIC") {
            std::string dbType = "DECIMAL";
            if (!scale.empty()) {
                dbType += "(" + precision + "," + scale + ")";
            } else {
                dbType += "(" + precision + ")";
            }
            return dbType;
        }
        if (appType == "FLOAT") return "FLOAT";
        if (appType == "DOUBLE") return "DOUBLE";
        if (appType == "BLOB") return "LONGBLOB";
    } else if (dbEngine == "sqlite") {
        // SQLite has dynamic typing, but we still provide mappings
        if (appType == "INTEGER") return "INTEGER";
        if (appType == "BIGINT") return "INTEGER";
        if (appType == "VARCHAR") return "TEXT";
        if (appType == "TEXT") return "TEXT";
        if (appType == "BOOLEAN") return "INTEGER";
        if (appType == "DATE") return "TEXT";
        if (appType == "DATETIME") return "TEXT";
        if (appType == "DECIMAL" || appType == "NUMERIC") {
            // SQLite doesn't enforce precision/scale, but we can include it for documentation
            return "DECIMAL";
        }
        if (appType == "FLOAT") return "REAL";
        if (appType == "DOUBLE") return "REAL";
        if (appType == "BLOB") return "BLOB";
    }

    // Default fallback
    return "TEXT";
}

bool SchemaLoader::addDBField(std::shared_ptr<DatabaseInterface> db,
                              const std::string& tableName,
                              const FieldDefinition& field) {
    try {
        // std::string dbEngine = configuration_.databaseEngine;
        std::string dbEngine = getDbEngine();
        std::string sql = generateAddFieldSQL(tableName, field, dbEngine);

        if (!sql.empty()) {
            db->executeQuery(sql);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error adding field " << field.name << " to table " << tableName
                  << ": " << e.what() << std::endl;
        return false;
    }
}

std::string SchemaLoader::generateAddFieldSQL(const std::string& tableName,
                                                const FieldDefinition& field,
                                                const std::string& dbEngine) {
    std::ostringstream sql;

    sql << "ALTER TABLE " << "\"" + tableName + "\"" << " ADD COLUMN " << field.name << " ";

    std::string dbType = mapAppTypeToDBType(field.type, dbEngine, field.precision, field.scale, field.maxLength);
    sql << dbType;

    if (!field.nullable) {
        sql << " NOT NULL";
    }

    if (!field.defaultValue.empty()) {
        sql << " DEFAULT " << field.defaultValue;
    }

    return sql.str();
}

bool SchemaLoader::modifyDBField(std::shared_ptr<DatabaseInterface> db,
                                 const std::string& tableName,
                                 const FieldDefinition& field) {
    try {
        // std::string dbEngine = configuration_.databaseEngine;
        std::string dbEngine = getDbEngine();

        // Note: Field modification is complex and varies significantly between databases
        // This is a simplified implementation
        std::string sql = generateModifyFieldSQL(tableName, field, dbEngine);

        if (!sql.empty()) {
            db->executeQuery(sql);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error modifying field " << field.name << " in table " << tableName
                  << ": " << e.what() << std::endl;
        return false;
    }
}

std::string SchemaLoader::generateModifyFieldSQL(const std::string& tableName,
                                                   const FieldDefinition& field,
                                                   const std::string& dbEngine) {
    // Field modification syntax varies greatly between databases
    // This is a simplified approach - in practice, you might need to:
    // 1. Add new column
    // 2. Copy data from old column to new column
    // 3. Drop old column
    // 4. Rename new column to old column name

    if (dbEngine == "sqlite") {
        // SQLite has limited ALTER TABLE support
        // In practice, you would need to recreate the table
        return ""; // Not implemented for simplicity
    }

    std::ostringstream sql;

    if (dbEngine == "postgresql") {
        sql << "ALTER TABLE " << "\"" + tableName + "\"" << " ALTER COLUMN " << field.name << " TYPE ";
        std::string dbType = mapAppTypeToDBType(field.type, dbEngine, field.precision, field.scale, field.maxLength);
        sql << dbType;

        if (!field.nullable) {
            sql << ", ALTER COLUMN " << field.name << " SET NOT NULL";
        } else {
            sql << ", ALTER COLUMN " << field.name << " DROP NOT NULL";
        }

        if (!field.defaultValue.empty()) {
            sql << ", ALTER COLUMN " << field.name << " SET DEFAULT " << field.defaultValue;
        } else {
            sql << ", ALTER COLUMN " << field.name << " DROP DEFAULT";
        }
    } else if (dbEngine == "mysql") {
        sql << "ALTER TABLE " << "\"" + tableName + "\"" << " MODIFY COLUMN " << field.name << " ";
        std::string dbType = mapAppTypeToDBType(field.type, dbEngine, field.precision, field.scale, field.maxLength);
        sql << dbType;

        if (!field.nullable) {
            sql << " NOT NULL";
        }

        if (!field.defaultValue.empty()) {
            sql << " DEFAULT " << field.defaultValue;
        }
    }

    return sql.str();
}

bool SchemaLoader::addDBForeignKey(std::shared_ptr<DatabaseInterface> db,
                                   const std::string& tableName,
                                   const ForeignKeyField& fk) {
    try {
        std::string constraintName = tableName + "_" + fk.name + "_fkey";
        std::ostringstream sql;

        sql << "ALTER TABLE " << "\"" + tableName + "\""
            << " ADD CONSTRAINT " << constraintName
            << " FOREIGN KEY (" << fk.name << ") REFERENCES "
            << fk.references << "(" << fk.toField << ")";

        if (!fk.onUpdate.empty()) {
            sql << " ON UPDATE " << fk.onUpdate;
        }

        if (!fk.onDelete.empty()) {
            sql << " ON DELETE " << fk.onDelete;
        }

        db->executeQuery(sql.str());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error adding foreign key " << fk.name << " to table " << tableName
                  << ": " << e.what() << std::endl;
        return false;
    }
}

void SchemaLoader::syncEntitiesWithDatabase() {
    // Copy entity from database in main variables
    entities_ = dbEntities_;

    // Copy views
    views_ = dbViews_;

    // Copy materialized views
    materializedViews_ = dbMaterializedViews_;

    // Copy stored procedures
    storedProcedures_ = dbStoredProcedures_;

    // Copy triggers
    triggers_ = dbTriggers_;

    // Copy function database
    databaseFunctions_ = dbDatabaseFunctions_;

    // Copy sequences
    sequences_ = dbSequences_;
    processReverseRelationships(entities_);

    std::cout << "Schema entities synchronized from database successfully" << std::endl;
}

bool SchemaLoader::saveSchemaFromDatabase(
    const std::shared_ptr<DatabaseInterface>& db_,
    const std::string& outputFile
) {
    try {
        if (!db_) {
            std::cerr << "Database interface is null" << std::endl;
            return false;
        }

        // Load schema from database
        if (!loadSchemaFromDB(db_)) {
            std::cerr << "Failed to load schema from database" << std::endl;
            return false;
        }

        const auto dbConfig = db_->getDatabaseConfig();
        const std::string dbEngine = dbConfig.driver;

        // Generate XML-view schema
        pugi::xml_document doc;
        auto declaration = doc.append_child(pugi::node_declaration);
        declaration.append_attribute("version") = "1.0";
        declaration.append_attribute("encoding") = "UTF-8";

        auto appNode = doc.append_child("Application");

        // Add main information o application
        const std::string schemaName = schemaMetadata_.name.empty()
            ? "Generated Database Schema"
            : schemaMetadata_.name;
        const std::string schemaVersion = schemaMetadata_.version.empty()
            ? "1.0.0"
            : schemaMetadata_.version;
        const std::string schemaDescription = schemaMetadata_.description.empty()
            ? "Schema generated from existing database"
            : schemaMetadata_.description;
        appNode.append_child("Name").text() = schemaName.c_str();
        appNode.append_child("Version").text() = schemaVersion.c_str();
        appNode.append_child("Description").text() = schemaDescription.c_str();

        // Add configuration
        auto configNode = appNode.append_child("Configuration");
        const std::string dbConnectionString = configuration_.dbConnectionString.empty()
            ? dbConfig.connectionString
            : configuration_.dbConnectionString;
        const std::string logLevel = configuration_.logLevel.empty() ? "INFO" : configuration_.logLevel;
        const int timeout = configuration_.timeout > 0 ? configuration_.timeout : 30;
        const std::string rawDatabaseEngine = configuration_.databaseEngine.empty()
            ? dbEngine
            : configuration_.databaseEngine;
        const std::string databaseEngine = normalizeDatabaseEngine(rawDatabaseEngine);

        const std::string safeConnectionString = sanitizeConnectionString(dbConnectionString);

        configNode.append_child("DbConnectionString").text() = safeConnectionString.c_str();
        configNode.append_child("LogLevel").text() = logLevel.c_str();
        configNode.append_child("Timeout").text() = std::to_string(timeout).c_str();
        appendTextIfNotEmpty(configNode, "DatabaseEngine", databaseEngine);

        // Add structure data
        auto dataStructureNode = appNode.append_child("DataStructure");

        // Process entity (table)
        for (const auto& entityPair : dbEntities_) {
            const EntityDefinition& entity = entityPair.second;
            auto entityNode = dataStructureNode.append_child("Entity");
            entityNode.append_attribute("name") = entity.name.c_str();
            entityNode.append_attribute("tableName") = entity.tableName.c_str();
            entityNode.append_attribute("isPartitioned") = entity.isPartitioned;

            if (!entity.verboseName.empty()) {
                entityNode.append_attribute("verboseName") = entity.verboseName.c_str();
            }
            if (!entity.verboseNamePlural.empty()) {
                entityNode.append_attribute("verboseNamePlural") = entity.verboseNamePlural.c_str();
            }
            if (!entity.description.empty()) {
                entityNode.append_attribute("description") = entity.description.c_str();
            }
            if (!entity.tablespace.empty()) {
                entityNode.append_attribute("tablespace") = entity.tablespace.c_str();
            }

            // Add fields
            for (const auto& field : entity.fields) {
                auto fieldNode = entityNode.append_child("Field");
                appendFieldAttributes(fieldNode, field);
            }

            // Add foreign keys
            for (const auto& fk : entity.foreignKeys) {
                auto fkNode = entityNode.append_child("ForeignKeyField");
                appendFieldAttributes(fkNode, fk);
                fkNode.append_attribute("references") = fk.references.c_str();
                const std::string onDelete = normalizeForeignKeyAction(fk.onDelete);
                const std::string onUpdate = normalizeForeignKeyAction(fk.onUpdate);
                appendAttributeIfNotEmpty(fkNode, "onDelete", onDelete);
                appendAttributeIfNotEmpty(fkNode, "onUpdate", onUpdate);
                appendAttributeIfNotEmpty(fkNode, "toField", fk.toField);
                appendAttributeIfNotEmpty(fkNode, "relatedName", fk.relatedName);
            }

            // Add indexes
            for (const auto& index : entity.indexes) {
                auto indexNode = entityNode.append_child("Index");
                indexNode.append_attribute("name") = index.name.c_str();
                indexNode.append_attribute("unique") = index.unique;
                appendAttributeIfNotEmpty(indexNode, "type", index.type);
                indexNode.append_attribute("concurrently") = index.concurrently;
                appendAttributeIfNotEmpty(indexNode, "tablespace", index.tablespace);

                for (const auto& fieldName : index.fieldNames) {
                    indexNode.append_child("FieldName").text() = fieldName.c_str();
                }
            }

            // Add constraints and CHECK constraints
            for (const auto& constraint : entity.constraints) {
                appendConstraint(entityNode, constraint);
            }
            for (const auto& checkConstraint : entity.checkConstraints) {
                appendCheckConstraint(entityNode, checkConstraint);
            }

            // Add function entity
            if (!entity.functions.empty()) {
                auto functionsNode = entityNode.append_child("EntityFunctions");
                for (const auto& function : entity.functions) {
                    appendFunction(functionsNode, function);
                }
            }

            appendTextIfNotEmpty(entityNode, "Tablespace", entity.tablespace);
            appendOptions(entityNode, entity.options);
            appendPartitions(entityNode, entity.partitions);
        }

        appendViews(dataStructureNode, dbViews_);
        appendMaterializedViews(dataStructureNode, dbMaterializedViews_);
        appendDatabaseFunctions(dataStructureNode, dbDatabaseFunctions_);
        appendStoredProcedures(dataStructureNode, dbStoredProcedures_);
        appendTriggers(dataStructureNode, dbTriggers_);
        appendSequences(dataStructureNode, dbSequences_);
        appendDatabaseMapping(appNode, databaseMapping_, dbEntities_, dbEngine);

        const std::filesystem::path outputPath(outputFile);
        if (!outputPath.parent_path().empty()) {
            std::error_code ec;
            std::filesystem::create_directories(outputPath.parent_path(), ec);
            if (ec) {
                std::cerr << "Failed to create schema output directory: "
                          << outputPath.parent_path().string()
                          << " (" << ec.message() << ")" << std::endl;
                return false;
            }
        }

        // Save XML in file
        if (!doc.save_file(outputFile.c_str(), PUGIXML_TEXT("  "))) {
            std::cerr << "Failed to save generated schema XML to: " << outputFile << std::endl;
            return false;
        }

        std::error_code fileSizeError;
        const auto schemaSize = std::filesystem::file_size(outputPath, fileSizeError);
        if (fileSizeError || schemaSize == 0) {
            std::cerr << "Generated schema XML is missing or empty: " << outputFile;
            if (fileSizeError) {
                std::cerr << " (" << fileSizeError.message() << ")";
            }
            std::cerr << std::endl;
            return false;
        }

        std::cout << "Schema generated from database and saved to: " << outputFile << std::endl;

        // Load generated schema in main entities_...
        return loadSchemaFromFile(outputFile);
    } catch (const std::exception& e) {
        std::cerr << "Error applying schema from database: " << e.what() << std::endl;
        return false;
    }
}
