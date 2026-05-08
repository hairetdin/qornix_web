/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "database_schema_exporter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace {

std::string trim(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }

    return std::string(begin, end);
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string normalizeDriverName(const std::string& driverName) {
    const auto lower = toLower(trim(driverName));
    if (lower == "sqlite3") {
        return "sqlite";
    }
    if (lower == "postgres" || lower == "postgresql") {
        return "postgresql";
    }
    if (lower == "mysql" || lower == "mariadb") {
        return "mysql";
    }
    return lower;
}


bool isXsdSupportedDatabaseEngine(const std::string& engine) {
    return engine == "sqlite" || engine == "postgresql" || engine == "mysql";
}

std::string xsdDatabaseEngineOrEmpty(const std::string& engine) {
    return isXsdSupportedDatabaseEngine(engine) ? engine : std::string{};
}

std::string makePascalCaseName(const std::string& tableName) {
    std::string result;
    bool capitalizeNext = true;

    for (const auto ch : tableName) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            if (capitalizeNext) {
                result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                capitalizeNext = false;
            } else {
                result.push_back(ch);
            }
        } else {
            capitalizeNext = true;
        }
    }

    if (result.empty()) {
        return "Entity";
    }

    if (std::isdigit(static_cast<unsigned char>(result.front()))) {
        result.insert(result.begin(), 'E');
    }

    return result;
}

std::string uniqueEntityName(const std::string& tableName, std::set<std::string>& usedNames) {
    auto baseName = makePascalCaseName(tableName);
    auto candidate = baseName;
    int suffix = 2;
    while (usedNames.find(candidate) != usedNames.end()) {
        candidate = baseName + std::to_string(suffix++);
    }
    usedNames.insert(candidate);
    return candidate;
}

std::string stripTypeArguments(std::string dbType) {
    dbType = trim(dbType);
    const auto pos = dbType.find('(');
    if (pos != std::string::npos) {
        dbType = dbType.substr(0, pos);
    }
    return trim(dbType);
}

std::string extractFirstTypeArgument(const std::string& dbType) {
    const auto open = dbType.find('(');
    const auto close = dbType.find(')', open == std::string::npos ? 0 : open + 1);
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
        return {};
    }
    return trim(dbType.substr(open + 1, close - open - 1));
}

std::string normalizeColumnType(
    const std::string& rawType,
    const std::string& columnName,
    const std::string& tableName,
    std::vector<std::string>& warnings
) {
    const auto base = toUpper(stripTypeArguments(rawType));

    if (base.empty()) {
        warnings.push_back("Column '" + tableName + "." + columnName + "' has empty database type. Exported as TEXT.");
        return "TEXT";
    }

    if (base == "INT") return "INTEGER";
    if (base == "INTEGER") return "INTEGER";
    if (base == "BIGINT") return "BIGINT";
    if (base == "SMALLINT") return "SMALLINT";
    if (base == "MEDIUMINT") return "MEDIUMINT";
    if (base == "TINYINT") return "TINYINT";
    if (base == "SERIAL") return "SERIAL";
    if (base == "BIGSERIAL") return "BIGSERIAL";

    if (base == "VARCHAR" || base == "CHAR" || base == "CHARACTER VARYING") return "VARCHAR";
    if (base == "TEXT" || base == "CLOB") return "TEXT";
    if (base == "TINYTEXT") return "TINYTEXT";
    if (base == "MEDIUMTEXT") return "MEDIUMTEXT";
    if (base == "LONGTEXT") return "LONGTEXT";

    if (base == "REAL") return "REAL";
    if (base == "FLOAT") return "FLOAT";
    if (base == "DOUBLE" || base == "DOUBLE PRECISION") return "DOUBLE";
    if (base == "NUMERIC") return "DECIMAL";
    if (base == "DECIMAL") return "DECIMAL";

    if (base == "BOOL") return "BOOLEAN";
    if (base == "BOOLEAN") return "BOOLEAN";

    if (base == "DATE") return "DATE";
    if (base == "TIME") return "TIME";
    if (base == "DATETIME") return "DATETIME";
    if (base == "TIMESTAMP" || base == "TIMESTAMPTZ") return "TIMESTAMP";
    if (base == "YEAR") return "YEAR";

    if (base == "JSON") return "JSON";
    if (base == "JSONB") return "JSONB";
    if (base == "UUID") return "UUID";

    if (base == "BLOB") return "BLOB";
    if (base == "BYTEA") return "BYTEA";
    if (base == "TINYBLOB") return "TINYBLOB";
    if (base == "MEDIUMBLOB") return "MEDIUMBLOB";
    if (base == "LONGBLOB") return "LONGBLOB";

    if (base == "INET") return "INET";
    if (base == "CIDR") return "CIDR";
    if (base == "MACADDR") return "MACADDR";
    if (base == "ENUM") return "ENUM";
    if (base == "SET") return "SET";

    warnings.push_back(
        "Column '" + tableName + "." + columnName + "' has unsupported database type '" + rawType + "'. Exported as TEXT."
    );
    return "TEXT";
}

std::string normalizeIndexType(const std::string& rawType) {
    const auto type = toUpper(trim(rawType));
    if (type == "HASH") return "HASH";
    if (type == "GIST") return "GIST";
    if (type == "GIN") return "GIN";
    if (type == "BRIN") return "BRIN";
    if (type == "SPGIST") return "SPGIST";
    return "BTREE";
}

std::string normalizeFkAction(const std::string& rawAction) {
    auto action = toUpper(trim(rawAction));
    std::replace(action.begin(), action.end(), ' ', '_');

    if (action == "CASCADE") return "CASCADE";
    if (action == "SET_NULL") return "SET_NULL";
    if (action == "SET_DEFAULT") return "SET_DEFAULT";
    if (action == "RESTRICT") return "RESTRICT";
    if (action == "NO_ACTION" || action.empty()) return "NO_ACTION";
    return "NO_ACTION";
}

std::string normalizeTriggerEvent(const std::string& rawEvent, std::vector<std::string>& warnings, const std::string& triggerName) {
    const auto event = toUpper(trim(rawEvent));
    if (event == "INSERT" || event == "UPDATE" || event == "DELETE" || event == "TRUNCATE") {
        return event;
    }
    warnings.push_back("Trigger '" + triggerName + "' has unknown event '" + rawEvent + "'. Exported as INSERT for XSD compatibility.");
    return "INSERT";
}

std::string normalizeTriggerTiming(const std::string& rawTiming, std::vector<std::string>& warnings, const std::string& triggerName) {
    const auto timing = toUpper(trim(rawTiming));
    if (timing == "BEFORE" || timing == "AFTER" || timing == "INSTEAD_OF") {
        return timing;
    }
    warnings.push_back("Trigger '" + triggerName + "' has unknown timing '" + rawTiming + "'. Exported as AFTER for XSD compatibility.");
    return "AFTER";
}

std::map<std::string, std::string> buildTableToEntityNameMap(const DatabaseSnapshot& snapshot) {
    std::map<std::string, std::string> result;
    std::set<std::string> usedNames;
    for (const auto& table : snapshot.tables) {
        result[table.name] = uniqueEntityName(table.name, usedNames);
    }
    return result;
}

std::map<std::string, std::vector<DbForeignKey>> buildForeignKeysByTable(const DatabaseSnapshot& snapshot) {
    std::map<std::string, std::vector<DbForeignKey>> result;
    for (const auto& foreignKey : snapshot.foreignKeys) {
        result[foreignKey.tableName].push_back(foreignKey);
    }
    return result;
}

bool isForeignKeyColumn(const std::vector<DbForeignKey>& foreignKeys, const std::string& columnName, DbForeignKey* outForeignKey) {
    for (const auto& foreignKey : foreignKeys) {
        if (foreignKey.columnName == columnName) {
            if (outForeignKey != nullptr) {
                *outForeignKey = foreignKey;
            }
            return true;
        }
    }
    return false;
}

FieldDefinition toFieldDefinition(
    const DbColumn& column,
    std::vector<std::string>& warnings
) {
    FieldDefinition field;
    field.name = column.name;
    field.type = normalizeColumnType(column.type, column.name, column.tableName, warnings);
    field.nullable = column.nullable;
    field.primaryKey = column.primaryKey;
    field.unique = column.unique;
    field.defaultValue = column.defaultValue;
    field.collation = column.collation;
    field.computedExpression = column.computedExpression;
    field.isComputed = !column.computedExpression.empty();
    field.autoIncrement = column.autoIncrement;
    field.verboseName = column.name;
    field.description.clear();

    field.maxLength.clear();
    const auto typeArgument = extractFirstTypeArgument(column.type);
    if (field.type == "VARCHAR" && !typeArgument.empty()) {
        field.maxLength = typeArgument;
    }

    return field;
}

ForeignKeyField toForeignKeyField(
    const DbColumn& column,
    const DbForeignKey& foreignKey,
    std::vector<std::string>& warnings
) {
    const auto base = toFieldDefinition(column, warnings);

    ForeignKeyField field;
    field.name = base.name;
    field.type = base.type;
    field.nullable = base.nullable;
    field.primaryKey = base.primaryKey;
    field.unique = base.unique;
    field.maxLength = base.maxLength;
    field.defaultValue = base.defaultValue;
    field.verboseName = base.verboseName;
    field.description = base.description;
    field.autoIncrement = base.autoIncrement;
    field.collation = base.collation;
    field.computedExpression = base.computedExpression;
    field.isComputed = base.isComputed;
    field.precision = base.precision;
    field.scale = base.scale;
    field.enumValues = base.enumValues;

    field.references = foreignKey.referencedTableName;
    field.toField = foreignKey.referencedColumnName.empty() ? "id" : foreignKey.referencedColumnName;
    field.onDelete = normalizeFkAction(foreignKey.onDelete);
    field.onUpdate = normalizeFkAction(foreignKey.onUpdate);
    field.relatedName.clear();
    return field;
}

IndexDefinition toIndexDefinition(const DbIndex& dbIndex) {
    IndexDefinition index;
    index.name = dbIndex.name;
    index.fieldNames = dbIndex.columnNames;
    index.unique = dbIndex.unique;
    index.type = normalizeIndexType(dbIndex.type);
    index.whereClause = dbIndex.whereClause;
    index.isPartial = dbIndex.partial;
    return index;
}

ViewDefinition toViewDefinition(const DbView& dbView) {
    ViewDefinition view;
    view.name = dbView.name;
    view.viewName = dbView.name;
    view.query = dbView.definition;
    view.isUpdatable = false;
    return view;
}

TriggerDefinition toTriggerDefinition(
    const DbTrigger& dbTrigger,
    std::vector<std::string>& warnings
) {
    TriggerDefinition trigger;
    trigger.name = dbTrigger.name;
    trigger.triggerName = dbTrigger.name;
    trigger.tableName = dbTrigger.tableName;
    trigger.event = normalizeTriggerEvent(dbTrigger.event, warnings, dbTrigger.name);
    trigger.timing = normalizeTriggerTiming(dbTrigger.timing, warnings, dbTrigger.name);
    trigger.function = dbTrigger.definition.empty() ? dbTrigger.name : dbTrigger.definition;
    trigger.enabled = dbTrigger.enabled;
    return trigger;
}

std::string defaultApplicationName(const DatabaseSnapshot& snapshot) {
    if (!snapshot.metadata.databaseName.empty()) {
        return snapshot.metadata.databaseName + "_schema";
    }
    if (!snapshot.metadata.connectionName.empty()) {
        return snapshot.metadata.connectionName + "_schema";
    }
    return "database_schema";
}

} // namespace

DatabaseSchemaExportResult DatabaseSchemaExporter::exportSnapshot(
    const DatabaseSnapshot& snapshot,
    const DatabaseSchemaExportOptions& options
) {
    DatabaseSchemaExportResult result;

    auto document = std::make_unique<SchemaDocument>();
    document->sourceMetadata_.type = SchemaDocumentSourceType::DatabaseExport;
    document->sourceMetadata_.documentName = options.sourceDocumentName.empty()
        ? "database_snapshot"
        : options.sourceDocumentName;
    document->sourceMetadata_.databaseEngine = normalizeDriverName(snapshot.metadata.driverName);
    document->sourceMetadata_.schemaFormatVersion = options.schemaFormatVersion.empty()
        ? "1.0"
        : options.schemaFormatVersion;
    document->sourceMetadata_.createdAt = snapshot.metadata.capturedAt;

    document->metadata_.name = options.applicationName.empty()
        ? defaultApplicationName(snapshot)
        : options.applicationName;
    document->metadata_.version = options.applicationVersion.empty()
        ? "1.0"
        : options.applicationVersion;
    document->metadata_.description = options.description.empty()
        ? "Canonical schema exported from database snapshot."
        : options.description;
    document->metadata_.createdAt = snapshot.metadata.capturedAt;

    document->configuration_.dbConnectionString = snapshot.metadata.connectionName;
    document->configuration_.logLevel = "INFO";
    document->configuration_.timeout = 30;
    document->configuration_.databaseEngine = xsdDatabaseEngineOrEmpty(normalizeDriverName(snapshot.metadata.driverName));
    document->metadata_.configuration = document->configuration_;

    const auto tableToEntityName = buildTableToEntityNameMap(snapshot);
    const auto foreignKeysByTable = buildForeignKeysByTable(snapshot);

    for (const auto& table : snapshot.tables) {
        EntityDefinition entity;
        const auto entityNameIt = tableToEntityName.find(table.name);
        entity.name = entityNameIt == tableToEntityName.end() ? makePascalCaseName(table.name) : entityNameIt->second;
        entity.tableName = table.name;
        entity.verboseName = table.name;
        entity.verboseNamePlural = table.name;
        entity.description = options.includeRawSqlDescriptions ? table.rawSql : "";

        const auto tableColumns = snapshot.columnsForTable(table.name);
        const auto tableForeignKeys = foreignKeysByTable.find(table.name) == foreignKeysByTable.end()
            ? std::vector<DbForeignKey>{}
            : foreignKeysByTable.at(table.name);

        for (const auto& column : tableColumns) {
            DbForeignKey foreignKey;
            if (isForeignKeyColumn(tableForeignKeys, column.name, &foreignKey)) {
                entity.foreignKeys.push_back(toForeignKeyField(column, foreignKey, result.warnings));
            } else {
                entity.fields.push_back(toFieldDefinition(column, result.warnings));
            }
        }

        if (options.includeIndexes) {
            for (const auto& index : snapshot.indexesForTable(table.name)) {
                if (!index.name.empty() && !index.columnNames.empty()) {
                    entity.indexes.push_back(toIndexDefinition(index));
                }
            }
        }

        for (const auto& constraint : snapshot.constraints) {
            if (constraint.tableName != table.name) {
                continue;
            }
            ConstraintDefinition exportedConstraint;
            exportedConstraint.name = constraint.name.empty() ? constraint.type : constraint.name;
            exportedConstraint.constraintName = constraint.name.empty()
                ? ("constraint_" + table.name)
                : constraint.name;
            exportedConstraint.type = toUpper(constraint.type.empty() ? "CHECK" : constraint.type);
            if (exportedConstraint.type != "CHECK" && exportedConstraint.type != "UNIQUE" && exportedConstraint.type != "EXCLUDE") {
                result.warnings.push_back(
                    "Constraint '" + exportedConstraint.constraintName + "' on table '" + table.name +
                    "' has unsupported type '" + constraint.type + "'. Exported as CHECK."
                );
                exportedConstraint.type = "CHECK";
            }
            exportedConstraint.expression = constraint.expression.empty() ? constraint.rawSql : constraint.expression;
            entity.constraints.push_back(exportedConstraint);
        }

        document->entities_.push_back(std::move(entity));
    }

    if (options.includeViews) {
        for (const auto& view : snapshot.views) {
            if (view.materialized) {
                MaterializedViewDefinition materializedView;
                materializedView.name = view.name;
                materializedView.viewName = view.name;
                materializedView.query = view.definition;
                document->materializedViews_.push_back(std::move(materializedView));
            } else {
                document->views_.push_back(toViewDefinition(view));
            }
        }
    }

    if (options.includeTriggers) {
        for (const auto& trigger : snapshot.triggers) {
            document->triggers_.push_back(toTriggerDefinition(trigger, result.warnings));
        }
    }

    for (const auto& entity : document->entities_) {
        EntityMappingDefinition mapping;
        mapping.entityName = entity.name;
        mapping.tableName = entity.tableName;
        for (const auto& field : entity.fields) {
            FieldMappingDefinition fieldMapping;
            fieldMapping.fieldName = field.name;
            fieldMapping.columnName = field.name;
            if (document->configuration_.databaseEngine == "sqlite") {
                fieldMapping.sqliteType = field.type;
            } else if (document->configuration_.databaseEngine == "postgresql") {
                fieldMapping.postgresqlType = field.type;
            } else if (document->configuration_.databaseEngine == "mysql") {
                fieldMapping.mysqlType = field.type;
            }
            mapping.fieldMappings.push_back(std::move(fieldMapping));
        }
        for (const auto& field : entity.foreignKeys) {
            FieldMappingDefinition fieldMapping;
            fieldMapping.fieldName = field.name;
            fieldMapping.columnName = field.name;
            if (document->configuration_.databaseEngine == "sqlite") {
                fieldMapping.sqliteType = field.type;
            } else if (document->configuration_.databaseEngine == "postgresql") {
                fieldMapping.postgresqlType = field.type;
            } else if (document->configuration_.databaseEngine == "mysql") {
                fieldMapping.mysqlType = field.type;
            }
            mapping.fieldMappings.push_back(std::move(fieldMapping));
        }
        document->databaseMapping_.entityMappings.push_back(std::move(mapping));
    }
    document->metadata_.databaseMapping = document->databaseMapping_;

    result.success = true;
    result.document = std::move(document);
    return result;
}

std::string DatabaseSchemaExporter::exportSnapshotToCanonicalXml(
    const DatabaseSnapshot& snapshot,
    const DatabaseSchemaExportOptions& options
) {
    auto result = exportSnapshot(snapshot, options);
    if (!result.ok() || result.document == nullptr) {
        return {};
    }
    return result.document->toCanonicalXml();
}

bool DatabaseSchemaExporter::saveSnapshotAsCanonicalXml(
    const DatabaseSnapshot& snapshot,
    const std::string& outputFilePath,
    const DatabaseSchemaExportOptions& options
) {
    const auto xml = exportSnapshotToCanonicalXml(snapshot, options);
    if (xml.empty()) {
        return false;
    }

    std::ofstream output(outputFilePath, std::ios::binary);
    if (!output) {
        return false;
    }
    output << xml;
    return output.good();
}
