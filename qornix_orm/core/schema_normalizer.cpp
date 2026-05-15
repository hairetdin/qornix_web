/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_normalizer.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string>
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

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string collapseSpaces(const std::string& value) {
    std::string output;
    output.reserve(value.size());

    bool wasSpace = false;
    for (const auto ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!wasSpace) {
                output.push_back(' ');
                wasSpace = true;
            }
        } else {
            output.push_back(ch);
            wasSpace = false;
        }
    }

    return trim(output);
}

bool isQuotedIdentifier(const std::string& value) {
    const auto trimmed = trim(value);
    return trimmed.size() >= 2 &&
        ((trimmed.front() == '"' && trimmed.back() == '"') ||
         (trimmed.front() == '`' && trimmed.back() == '`') ||
         (trimmed.front() == '[' && trimmed.back() == ']'));
}

std::string normalizeIdentifier(const std::string& value) {
    return trim(value);
}

std::string normalizeKeyword(const std::string& value) {
    return toUpper(collapseSpaces(trim(value)));
}

std::string normalizeLanguage(const std::string& value) {
    return toLower(trim(value));
}

std::string stripSingleOuterParentheses(const std::string& value) {
    auto trimmed = trim(value);
    if (trimmed.size() < 2 || trimmed.front() != '(' || trimmed.back() != ')') {
        return trimmed;
    }

    int depth = 0;
    for (std::size_t i = 0; i < trimmed.size(); ++i) {
        if (trimmed[i] == '(') {
            ++depth;
        } else if (trimmed[i] == ')') {
            --depth;
            if (depth == 0 && i + 1 != trimmed.size()) {
                return trimmed;
            }
        }
    }

    return trim(trimmed.substr(1, trimmed.size() - 2));
}

std::string normalizeDefaultValue(const std::string& value) {
    auto normalized = collapseSpaces(stripSingleOuterParentheses(value));
    const auto upper = toUpper(normalized);

    if (upper.empty() || upper == "NULL") {
        return {};
    }
    if (upper == "CURRENT_TIMESTAMP" || upper == "CURRENT_DATE" || upper == "CURRENT_TIME") {
        return upper;
    }
    if (upper == "TRUE") {
        return "true";
    }
    if (upper == "FALSE") {
        return "false";
    }

    return normalized;
}

std::string normalizeType(const std::string& value) {
    auto normalized = toUpper(collapseSpaces(trim(value)));

    // Collapse common SQL/app aliases to a canonical type name. Keep this list
    // intentionally small; driver-specific precision rules belong
    // to DriverCapabilities and later diff/planner work.
    static const std::map<std::string, std::string> aliases = {
        {"INT", "INTEGER"},
        {"INT4", "INTEGER"},
        {"SIGNED INTEGER", "INTEGER"},
        {"UNSIGNED INTEGER", "INTEGER"},
        {"BIGINT", "BIGINT"},
        {"INT8", "BIGINT"},
        {"SMALLINT", "SMALLINT"},
        {"INT2", "SMALLINT"},
        {"BOOL", "BOOLEAN"},
        {"BOOLEAN", "BOOLEAN"},
        {"VARCHAR", "VARCHAR"},
        {"CHARACTER VARYING", "VARCHAR"},
        {"TEXT", "TEXT"},
        {"STRING", "VARCHAR"},
        {"DOUBLE PRECISION", "DOUBLE"},
        {"FLOAT8", "DOUBLE"},
        {"REAL", "FLOAT"},
        {"DECIMAL", "DECIMAL"},
        {"NUMERIC", "DECIMAL"},
        {"TIMESTAMP WITHOUT TIME ZONE", "TIMESTAMP"},
        {"DATETIME", "TIMESTAMP"}
    };

    const auto exact = aliases.find(normalized);
    if (exact != aliases.end()) {
        return exact->second;
    }

    // Preserve type parameters while normalizing the base type.
    const auto open = normalized.find('(');
    if (open != std::string::npos && normalized.back() == ')') {
        const auto base = normalized.substr(0, open);
        const auto params = normalized.substr(open);
        const auto baseAlias = aliases.find(base);
        if (baseAlias != aliases.end()) {
            return baseAlias->second + params;
        }
    }

    return normalized;
}

void warnIfQuotedIdentifier(
    std::vector<SchemaNormalizationWarning>& warnings,
    const std::string& value,
    const std::string& elementPath
) {
    if (isQuotedIdentifier(value)) {
        warnings.push_back({
            "QORNIX_SCHEMA_NORMALIZE_QUOTED_IDENTIFIER",
            "Quoted identifiers are preserved and may remain database-specific.",
            elementPath
        });
    }
}

template <typename T, typename KeyFn>
void stableSortBy(std::vector<T>& values, KeyFn keyFn) {
    std::stable_sort(values.begin(), values.end(), [&](const T& lhs, const T& rhs) {
        return keyFn(lhs) < keyFn(rhs);
    });
}

std::string entitySortKey(const EntityDefinition& entity) {
    return !entity.tableName.empty() ? entity.tableName : entity.name;
}

std::string viewSortKey(const ViewDefinition& view) {
    return !view.viewName.empty() ? view.viewName : view.name;
}

std::string materializedViewSortKey(const MaterializedViewDefinition& view) {
    return !view.viewName.empty() ? view.viewName : view.name;
}

std::string storedProcedureSortKey(const StoredProcedureDefinition& procedure) {
    return !procedure.procedureName.empty() ? procedure.procedureName : procedure.name;
}

std::string triggerSortKey(const TriggerDefinition& trigger) {
    return !trigger.triggerName.empty() ? trigger.triggerName : trigger.name;
}

std::string databaseFunctionSortKey(const DatabaseFunctionDefinition& function) {
    return !function.functionName.empty() ? function.functionName : function.name;
}

void normalizeField(
    FieldDefinition& field,
    std::vector<SchemaNormalizationWarning>& warnings,
    const SchemaNormalizationOptions& options,
    const std::string& path
) {
    if (options.normalizeIdentifierWhitespace) {
        warnIfQuotedIdentifier(warnings, field.name, path + "/@name");
        field.name = normalizeIdentifier(field.name);
        field.references = normalizeIdentifier(field.references);
    }

    if (options.normalizeTypeAliases) {
        field.type = normalizeType(field.type);
    }

    if (options.normalizeDefaultValues) {
        field.defaultValue = normalizeDefaultValue(field.defaultValue);
    }

    field.maxLength = trim(field.maxLength);
    field.precision = trim(field.precision);
    field.scale = trim(field.scale);
    field.collation = trim(field.collation);
    field.computedExpression = collapseSpaces(field.computedExpression);

    for (auto& enumValue : field.enumValues) {
        enumValue = trim(enumValue);
    }
}

void normalizeForeignKey(
    ForeignKeyField& field,
    std::vector<SchemaNormalizationWarning>& warnings,
    const SchemaNormalizationOptions& options,
    const std::string& path
) {
    normalizeField(field, warnings, options, path);

    if (options.normalizeIdentifierWhitespace) {
        field.references = normalizeIdentifier(field.references);
        field.toField = normalizeIdentifier(field.toField.empty() ? "id" : field.toField);
        field.relatedName = normalizeIdentifier(field.relatedName);
    }

    if (options.normalizeActionKeywords) {
        field.onDelete = normalizeKeyword(field.onDelete.empty() ? "NO_ACTION" : field.onDelete);
        field.onUpdate = normalizeKeyword(field.onUpdate.empty() ? "NO_ACTION" : field.onUpdate);
    }
}

void normalizeIndex(
    IndexDefinition& index,
    std::vector<SchemaNormalizationWarning>& warnings,
    const SchemaNormalizationOptions& options,
    const std::string& path
) {
    if (options.normalizeIdentifierWhitespace) {
        warnIfQuotedIdentifier(warnings, index.name, path + "/@name");
        index.name = normalizeIdentifier(index.name);
        for (auto& fieldName : index.fieldNames) {
            warnIfQuotedIdentifier(warnings, fieldName, path + "/FieldName");
            fieldName = normalizeIdentifier(fieldName);
        }
    }

    if (options.normalizeActionKeywords) {
        index.type = normalizeKeyword(index.type.empty() ? "BTREE" : index.type);
        index.method = normalizeLanguage(index.method);
    }

    index.tablespace = trim(index.tablespace);
    index.whereClause = collapseSpaces(index.whereClause);
    for (auto& op : index.operators) {
        op = trim(op);
    }
}

void normalizeConstraint(ConstraintDefinition& constraint, const SchemaNormalizationOptions& options) {
    if (options.normalizeIdentifierWhitespace) {
        constraint.name = normalizeIdentifier(constraint.name);
        constraint.constraintName = normalizeIdentifier(constraint.constraintName);
    }
    if (options.normalizeActionKeywords) {
        constraint.type = normalizeKeyword(constraint.type);
    }
    constraint.expression = collapseSpaces(constraint.expression);
}

void normalizeFunction(FunctionDefinition& function, const SchemaNormalizationOptions& options) {
    if (options.normalizeIdentifierWhitespace) {
        function.name = normalizeIdentifier(function.name);
        function.fileName = trim(function.fileName);
    }
    function.language = normalizeLanguage(function.language.empty() ? "python" : function.language);
    function.returnType = options.normalizeTypeAliases ? normalizeType(function.returnType) : trim(function.returnType);
    function.code = trim(function.code);

    for (auto& parameter : function.parameters) {
        parameter.name = normalizeIdentifier(parameter.name);
        parameter.type = options.normalizeTypeAliases ? normalizeType(parameter.type) : trim(parameter.type);
        parameter.mode = normalizeKeyword(parameter.mode);
    }
}

void normalizeEntity(
    EntityDefinition& entity,
    std::vector<SchemaNormalizationWarning>& warnings,
    const SchemaNormalizationOptions& options
) {
    if (options.normalizeIdentifierWhitespace) {
        warnIfQuotedIdentifier(warnings, entity.name, "/Application/DataStructure/Entity/@name");
        warnIfQuotedIdentifier(warnings, entity.tableName, "/Application/DataStructure/Entity/@tableName");
        entity.name = normalizeIdentifier(entity.name);
        entity.tableName = normalizeIdentifier(entity.tableName.empty() ? entity.name : entity.tableName);
    }

    entity.tablespace = trim(entity.tablespace);

    for (auto& field : entity.fields) {
        normalizeField(field, warnings, options, "/Application/DataStructure/Entity[@name='" + entity.name + "']/Field[@name='" + field.name + "']");
    }
    for (auto& fk : entity.foreignKeys) {
        normalizeForeignKey(fk, warnings, options, "/Application/DataStructure/Entity[@name='" + entity.name + "']/ForeignKeyField[@name='" + fk.name + "']");
    }
    for (auto& index : entity.indexes) {
        normalizeIndex(index, warnings, options, "/Application/DataStructure/Entity[@name='" + entity.name + "']/Index[@name='" + index.name + "']");
    }
    for (auto& constraint : entity.constraints) {
        normalizeConstraint(constraint, options);
    }
    for (auto& checkConstraint : entity.checkConstraints) {
        checkConstraint.name = normalizeIdentifier(checkConstraint.name);
        checkConstraint.expression = collapseSpaces(checkConstraint.expression);
    }
    for (auto& function : entity.functions) {
        normalizeFunction(function, options);
    }

    if (options.sortComparableObjects) {
        stableSortBy(entity.indexes, [](const IndexDefinition& index) { return index.name; });
        stableSortBy(entity.constraints, [](const ConstraintDefinition& constraint) {
            return !constraint.constraintName.empty() ? constraint.constraintName : constraint.name;
        });
        stableSortBy(entity.checkConstraints, [](const CheckConstraintDefinition& constraint) { return constraint.name; });
        stableSortBy(entity.functions, [](const FunctionDefinition& function) { return function.name; });
    }
}

void normalizeView(ViewDefinition& view, const SchemaNormalizationOptions& options) {
    if (options.normalizeIdentifierWhitespace) {
        view.name = normalizeIdentifier(view.name);
        view.viewName = normalizeIdentifier(view.viewName.empty() ? view.name : view.viewName);
    }
    view.query = collapseSpaces(view.query);
}

void normalizeMaterializedView(MaterializedViewDefinition& view, const SchemaNormalizationOptions& options) {
    if (options.normalizeIdentifierWhitespace) {
        view.name = normalizeIdentifier(view.name);
        view.viewName = normalizeIdentifier(view.viewName.empty() ? view.name : view.viewName);
    }
    view.refreshStrategy = normalizeKeyword(view.refreshStrategy);
    view.query = collapseSpaces(view.query);
}

void normalizeStoredProcedure(StoredProcedureDefinition& procedure, const SchemaNormalizationOptions& options) {
    if (options.normalizeIdentifierWhitespace) {
        procedure.name = normalizeIdentifier(procedure.name);
        procedure.procedureName = normalizeIdentifier(procedure.procedureName.empty() ? procedure.name : procedure.procedureName);
    }
    procedure.language = normalizeLanguage(procedure.language);
    procedure.security = normalizeKeyword(procedure.security);
    procedure.returnType = options.normalizeTypeAliases ? normalizeType(procedure.returnType) : trim(procedure.returnType);
    procedure.code = trim(procedure.code);
}

void normalizeTrigger(TriggerDefinition& trigger, const SchemaNormalizationOptions& options) {
    if (options.normalizeIdentifierWhitespace) {
        trigger.name = normalizeIdentifier(trigger.name);
        trigger.triggerName = normalizeIdentifier(trigger.triggerName.empty() ? trigger.name : trigger.triggerName);
        trigger.tableName = normalizeIdentifier(trigger.tableName);
        trigger.function = normalizeIdentifier(trigger.function);
    }
    trigger.event = normalizeKeyword(trigger.event);
    trigger.timing = normalizeKeyword(trigger.timing);
    trigger.condition = collapseSpaces(trigger.condition);
}

void normalizeDatabaseFunction(DatabaseFunctionDefinition& function, const SchemaNormalizationOptions& options) {
    if (options.normalizeIdentifierWhitespace) {
        function.name = normalizeIdentifier(function.name);
        function.functionName = normalizeIdentifier(function.functionName.empty() ? function.name : function.functionName);
    }
    function.language = normalizeLanguage(function.language);
    function.volatileType = normalizeKeyword(function.volatileType);
    function.security = normalizeKeyword(function.security);
    function.returnType = options.normalizeTypeAliases ? normalizeType(function.returnType) : trim(function.returnType);
    function.code = trim(function.code);
}

void normalizeSequence(SequenceDefinition& sequence, const SchemaNormalizationOptions& options) {
    if (options.normalizeIdentifierWhitespace) {
        sequence.name = normalizeIdentifier(sequence.name);
        sequence.tableName = normalizeIdentifier(sequence.tableName);
        sequence.columnName = normalizeIdentifier(sequence.columnName);
    }
}

void normalizeDatabaseMapping(DatabaseMappingDefinition& mapping, const SchemaNormalizationOptions& options) {
    for (auto& entityMapping : mapping.entityMappings) {
        entityMapping.entityName = normalizeIdentifier(entityMapping.entityName);
        entityMapping.tableName = normalizeIdentifier(entityMapping.tableName);
        for (auto& fieldMapping : entityMapping.fieldMappings) {
            fieldMapping.fieldName = normalizeIdentifier(fieldMapping.fieldName);
            fieldMapping.columnName = normalizeIdentifier(fieldMapping.columnName);
            if (options.normalizeTypeAliases) {
                fieldMapping.postgresqlType = normalizeType(fieldMapping.postgresqlType);
                fieldMapping.mysqlType = normalizeType(fieldMapping.mysqlType);
                fieldMapping.sqliteType = normalizeType(fieldMapping.sqliteType);
            }
        }
        if (options.sortComparableObjects) {
            stableSortBy(entityMapping.fieldMappings, [](const FieldMappingDefinition& fieldMapping) {
                return fieldMapping.fieldName;
            });
        }
    }

    for (auto& typeMap : mapping.typeMapping.typeMaps) {
        typeMap.appType = normalizeType(typeMap.appType);
        typeMap.postgresqlType = normalizeType(typeMap.postgresqlType);
        typeMap.mysqlType = normalizeType(typeMap.mysqlType);
        typeMap.sqliteType = normalizeType(typeMap.sqliteType);
    }

    if (options.sortComparableObjects) {
        stableSortBy(mapping.entityMappings, [](const EntityMappingDefinition& entityMapping) {
            return entityMapping.entityName;
        });
        stableSortBy(mapping.typeMapping.typeMaps, [](const TypeMapDefinition& typeMap) {
            return typeMap.appType;
        });
    }
}

} // namespace

SchemaNormalizationResult SchemaNormalizer::normalize(
    const SchemaDocument& document,
    const SchemaNormalizationOptions& options
) {
    SchemaNormalizationResult result;
    result.document = document;

    result.document.sourceMetadata_.documentName = trim(result.document.sourceMetadata_.documentName);
    result.document.sourceMetadata_.databaseEngine = normalizeLanguage(result.document.sourceMetadata_.databaseEngine);
    result.document.sourceMetadata_.schemaFormatVersion = trim(result.document.sourceMetadata_.schemaFormatVersion.empty()
        ? "1.0"
        : result.document.sourceMetadata_.schemaFormatVersion);

    result.document.metadata_.name = trim(result.document.metadata_.name);
    result.document.metadata_.version = trim(result.document.metadata_.version);
    result.document.metadata_.description = trim(result.document.metadata_.description);

    result.document.configuration_.dbConnectionString = trim(result.document.configuration_.dbConnectionString);
    result.document.configuration_.logLevel = normalizeKeyword(result.document.configuration_.logLevel.empty()
        ? "INFO"
        : result.document.configuration_.logLevel);
    result.document.configuration_.databaseEngine = normalizeLanguage(result.document.configuration_.databaseEngine);
    result.document.metadata_.configuration = result.document.configuration_;

    for (auto& entity : result.document.entities_) {
        normalizeEntity(entity, result.warnings, options);
    }
    for (auto& view : result.document.views_) {
        normalizeView(view, options);
    }
    for (auto& view : result.document.materializedViews_) {
        normalizeMaterializedView(view, options);
    }
    for (auto& procedure : result.document.storedProcedures_) {
        normalizeStoredProcedure(procedure, options);
    }
    for (auto& trigger : result.document.triggers_) {
        normalizeTrigger(trigger, options);
    }
    for (auto& function : result.document.databaseFunctions_) {
        normalizeDatabaseFunction(function, options);
    }
    for (auto& sequence : result.document.sequences_) {
        normalizeSequence(sequence, options);
    }
    normalizeDatabaseMapping(result.document.databaseMapping_, options);
    result.document.metadata_.databaseMapping = result.document.databaseMapping_;

    if (options.sortComparableObjects) {
        stableSortBy(result.document.entities_, entitySortKey);
        stableSortBy(result.document.views_, viewSortKey);
        stableSortBy(result.document.materializedViews_, materializedViewSortKey);
        stableSortBy(result.document.storedProcedures_, storedProcedureSortKey);
        stableSortBy(result.document.triggers_, triggerSortKey);
        stableSortBy(result.document.databaseFunctions_, databaseFunctionSortKey);
        stableSortBy(result.document.sequences_, [](const SequenceDefinition& sequence) { return sequence.name; });
    }

    return result;
}
