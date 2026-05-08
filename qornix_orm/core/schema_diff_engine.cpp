/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_diff_engine.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>

namespace {

std::string boolString(bool value) {
    return value ? "true" : "false";
}

std::string join(const std::vector<std::string>& values, const std::string& separator = ",") {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << values[i];
    }
    return out.str();
}

std::string quoteJson(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const auto ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out << "\\u00";
                    const char* hex = "0123456789abcdef";
                    out << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    out << ch;
                }
        }
    }
    out << '"';
    return out.str();
}

std::string entityKey(const EntityDefinition& entity) {
    return !entity.tableName.empty() ? entity.tableName : entity.name;
}

std::string viewKey(const ViewDefinition& view) {
    return !view.viewName.empty() ? view.viewName : view.name;
}

std::string triggerKey(const TriggerDefinition& trigger) {
    return !trigger.triggerName.empty() ? trigger.triggerName : trigger.name;
}

std::string sequenceKey(const SequenceDefinition& sequence) {
    if (!sequence.tableName.empty() && !sequence.columnName.empty()) {
        return sequence.tableName + "." + sequence.columnName;
    }
    return sequence.name;
}

std::string fieldKey(const FieldDefinition& field) {
    return field.name;
}

std::string foreignKeyKey(const ForeignKeyField& field) {
    if (!field.name.empty()) {
        return field.name;
    }
    return field.references + "." + field.toField;
}

std::string indexKey(const IndexDefinition& index) {
    if (!index.name.empty()) {
        return index.name;
    }
    return join(index.fieldNames, "+");
}

template <typename T, typename KeyFn>
std::map<std::string, T> toMap(const std::vector<T>& values, KeyFn keyFn) {
    std::map<std::string, T> result;
    for (const auto& value : values) {
        const auto key = keyFn(value);
        if (!key.empty()) {
            result[key] = value;
        }
    }
    return result;
}

void addOperation(
    SchemaDocumentDiff& diff,
    SchemaDiffOperationKind kind,
    std::string objectType,
    std::string objectPath,
    std::string objectName,
    std::string property,
    std::string desiredValue,
    std::string currentValue,
    std::string message
) {
    SchemaDiffOperation operation;
    operation.kind = kind;
    operation.objectType = std::move(objectType);
    operation.objectPath = std::move(objectPath);
    operation.objectName = std::move(objectName);
    operation.property = std::move(property);
    operation.desiredValue = std::move(desiredValue);
    operation.currentValue = std::move(currentValue);
    operation.message = std::move(message);
    diff.operations.push_back(std::move(operation));
}

void compareString(
    SchemaDocumentDiff& diff,
    SchemaDiffOperationKind kind,
    const std::string& objectType,
    const std::string& objectPath,
    const std::string& objectName,
    const std::string& property,
    const std::string& desiredValue,
    const std::string& currentValue
) {
    if (desiredValue != currentValue) {
        addOperation(
            diff,
            kind,
            objectType,
            objectPath,
            objectName,
            property,
            desiredValue,
            currentValue,
            property + " changed"
        );
    }
}

void compareBool(
    SchemaDocumentDiff& diff,
    SchemaDiffOperationKind kind,
    const std::string& objectType,
    const std::string& objectPath,
    const std::string& objectName,
    const std::string& property,
    bool desiredValue,
    bool currentValue
) {
    if (desiredValue != currentValue) {
        addOperation(
            diff,
            kind,
            objectType,
            objectPath,
            objectName,
            property,
            boolString(desiredValue),
            boolString(currentValue),
            property + " changed"
        );
    }
}

void compareLongLong(
    SchemaDocumentDiff& diff,
    SchemaDiffOperationKind kind,
    const std::string& objectType,
    const std::string& objectPath,
    const std::string& objectName,
    const std::string& property,
    long long desiredValue,
    long long currentValue
) {
    if (desiredValue != currentValue) {
        addOperation(
            diff,
            kind,
            objectType,
            objectPath,
            objectName,
            property,
            std::to_string(desiredValue),
            std::to_string(currentValue),
            property + " changed"
        );
    }
}

void compareField(
    SchemaDocumentDiff& diff,
    const std::string& entityPath,
    const FieldDefinition& desired,
    const FieldDefinition& current
) {
    const auto path = entityPath + "/fields/" + desired.name;
    compareString(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "type", desired.type, current.type);
    compareBool(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "nullable", desired.nullable, current.nullable);
    compareBool(diff, SchemaDiffOperationKind::PrimaryKeyChanged, "column", path, desired.name, "primaryKey", desired.primaryKey, current.primaryKey);
    compareBool(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "unique", desired.unique, current.unique);
    compareString(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "maxLength", desired.maxLength, current.maxLength);
    compareString(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "defaultValue", desired.defaultValue, current.defaultValue);
    compareBool(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "autoIncrement", desired.autoIncrement, current.autoIncrement);
    compareString(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "precision", desired.precision, current.precision);
    compareString(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "scale", desired.scale, current.scale);
    compareString(diff, SchemaDiffOperationKind::ColumnChanged, "column", path, desired.name, "references", desired.references, current.references);
}

void compareForeignKey(
    SchemaDocumentDiff& diff,
    const std::string& entityPath,
    const ForeignKeyField& desired,
    const ForeignKeyField& current
) {
    compareField(diff, entityPath, desired, current);
    const auto name = foreignKeyKey(desired);
    const auto path = entityPath + "/foreignKeys/" + name;
    compareString(diff, SchemaDiffOperationKind::ForeignKeyChanged, "foreign_key", path, name, "references", desired.references, current.references);
    compareString(diff, SchemaDiffOperationKind::ForeignKeyChanged, "foreign_key", path, name, "toField", desired.toField, current.toField);
    compareString(diff, SchemaDiffOperationKind::ForeignKeyChanged, "foreign_key", path, name, "onDelete", desired.onDelete, current.onDelete);
    compareString(diff, SchemaDiffOperationKind::ForeignKeyChanged, "foreign_key", path, name, "onUpdate", desired.onUpdate, current.onUpdate);
    compareString(diff, SchemaDiffOperationKind::ForeignKeyChanged, "foreign_key", path, name, "relatedName", desired.relatedName, current.relatedName);
}

void compareIndex(
    SchemaDocumentDiff& diff,
    const std::string& entityPath,
    const IndexDefinition& desired,
    const IndexDefinition& current
) {
    const auto name = indexKey(desired);
    const auto path = entityPath + "/indexes/" + name;
    compareString(diff, SchemaDiffOperationKind::IndexChanged, "index", path, name, "fields", join(desired.fieldNames), join(current.fieldNames));
    compareBool(diff, SchemaDiffOperationKind::IndexChanged, "index", path, name, "unique", desired.unique, current.unique);
    compareString(diff, SchemaDiffOperationKind::IndexChanged, "index", path, name, "type", desired.type, current.type);
    compareString(diff, SchemaDiffOperationKind::IndexChanged, "index", path, name, "method", desired.method, current.method);
    compareString(diff, SchemaDiffOperationKind::IndexChanged, "index", path, name, "whereClause", desired.whereClause, current.whereClause);
}

void compareEntity(
    SchemaDocumentDiff& diff,
    const EntityDefinition& desired,
    const EntityDefinition& current
) {
    const auto entityName = entityKey(desired);
    const auto path = "/entities/" + entityName;

    compareString(diff, SchemaDiffOperationKind::TableChanged, "table", path, entityName, "entityName", desired.name, current.name);
    compareString(diff, SchemaDiffOperationKind::TableChanged, "table", path, entityName, "tableName", desired.tableName, current.tableName);
    compareBool(diff, SchemaDiffOperationKind::TableChanged, "table", path, entityName, "isPartitioned", desired.isPartitioned, current.isPartitioned);

    const auto desiredFields = toMap(desired.fields, fieldKey);
    const auto currentFields = toMap(current.fields, fieldKey);

    for (const auto& item : desiredFields) {
        const auto currentIt = currentFields.find(item.first);
        if (currentIt == currentFields.end()) {
            addOperation(diff, SchemaDiffOperationKind::ColumnAdded, "column", path + "/fields/" + item.first, item.first, {}, item.first, {}, "Column exists only in desired schema");
        } else {
            compareField(diff, path, item.second, currentIt->second);
        }
    }
    for (const auto& item : currentFields) {
        if (desiredFields.find(item.first) == desiredFields.end()) {
            addOperation(diff, SchemaDiffOperationKind::ColumnOnlyInCurrent, "column", path + "/fields/" + item.first, item.first, {}, {}, item.first, "Column exists only in current database schema");
        }
    }

    const auto desiredForeignKeys = toMap(desired.foreignKeys, foreignKeyKey);
    const auto currentForeignKeys = toMap(current.foreignKeys, foreignKeyKey);

    for (const auto& item : desiredForeignKeys) {
        const auto currentIt = currentForeignKeys.find(item.first);
        if (currentIt == currentForeignKeys.end()) {
            addOperation(diff, SchemaDiffOperationKind::ForeignKeyAdded, "foreign_key", path + "/foreignKeys/" + item.first, item.first, {}, item.first, {}, "Foreign key exists only in desired schema");
        } else {
            compareForeignKey(diff, path, item.second, currentIt->second);
        }
    }
    for (const auto& item : currentForeignKeys) {
        if (desiredForeignKeys.find(item.first) == desiredForeignKeys.end()) {
            addOperation(diff, SchemaDiffOperationKind::ForeignKeyOnlyInCurrent, "foreign_key", path + "/foreignKeys/" + item.first, item.first, {}, {}, item.first, "Foreign key exists only in current database schema");
        }
    }

    const auto desiredIndexes = toMap(desired.indexes, indexKey);
    const auto currentIndexes = toMap(current.indexes, indexKey);

    for (const auto& item : desiredIndexes) {
        const auto currentIt = currentIndexes.find(item.first);
        if (currentIt == currentIndexes.end()) {
            addOperation(diff, SchemaDiffOperationKind::IndexAdded, "index", path + "/indexes/" + item.first, item.first, {}, item.first, {}, "Index exists only in desired schema");
        } else {
            compareIndex(diff, path, item.second, currentIt->second);
        }
    }
    for (const auto& item : currentIndexes) {
        if (desiredIndexes.find(item.first) == desiredIndexes.end()) {
            addOperation(diff, SchemaDiffOperationKind::IndexOnlyInCurrent, "index", path + "/indexes/" + item.first, item.first, {}, {}, item.first, "Index exists only in current database schema");
        }
    }
}

void compareView(
    SchemaDocumentDiff& diff,
    const ViewDefinition& desired,
    const ViewDefinition& current,
    bool compareSqlDefinitions
) {
    const auto name = viewKey(desired);
    const auto path = "/views/" + name;
    compareString(diff, SchemaDiffOperationKind::ViewChanged, "view", path, name, "name", desired.name, current.name);
    compareString(diff, SchemaDiffOperationKind::ViewChanged, "view", path, name, "viewName", desired.viewName, current.viewName);
    compareBool(diff, SchemaDiffOperationKind::ViewChanged, "view", path, name, "isUpdatable", desired.isUpdatable, current.isUpdatable);
    if (compareSqlDefinitions) {
        compareString(diff, SchemaDiffOperationKind::ViewChanged, "view", path, name, "query", desired.query, current.query);
    }
}

void compareTrigger(
    SchemaDocumentDiff& diff,
    const TriggerDefinition& desired,
    const TriggerDefinition& current,
    bool compareSqlDefinitions
) {
    const auto name = triggerKey(desired);
    const auto path = "/triggers/" + name;
    compareString(diff, SchemaDiffOperationKind::TriggerChanged, "trigger", path, name, "triggerName", desired.triggerName, current.triggerName);
    compareString(diff, SchemaDiffOperationKind::TriggerChanged, "trigger", path, name, "tableName", desired.tableName, current.tableName);
    compareString(diff, SchemaDiffOperationKind::TriggerChanged, "trigger", path, name, "event", desired.event, current.event);
    compareString(diff, SchemaDiffOperationKind::TriggerChanged, "trigger", path, name, "timing", desired.timing, current.timing);
    compareBool(diff, SchemaDiffOperationKind::TriggerChanged, "trigger", path, name, "enabled", desired.enabled, current.enabled);
    if (compareSqlDefinitions) {
        compareString(diff, SchemaDiffOperationKind::TriggerChanged, "trigger", path, name, "function", desired.function, current.function);
        compareString(diff, SchemaDiffOperationKind::TriggerChanged, "trigger", path, name, "condition", desired.condition, current.condition);
    }
}

void compareSequence(
    SchemaDocumentDiff& diff,
    const SequenceDefinition& desired,
    const SequenceDefinition& current
) {
    const auto name = sequenceKey(desired);
    const auto path = "/sequences/" + name;
    compareString(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "name", desired.name, current.name);
    compareString(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "tableName", desired.tableName, current.tableName);
    compareString(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "columnName", desired.columnName, current.columnName);
    compareLongLong(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "startValue", desired.startValue, current.startValue);
    compareLongLong(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "increment", desired.increment, current.increment);
    compareLongLong(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "minValue", desired.minValue, current.minValue);
    compareLongLong(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "maxValue", desired.maxValue, current.maxValue);
    compareBool(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "cycle", desired.cycle, current.cycle);
    compareLongLong(diff, SchemaDiffOperationKind::SequenceChanged, "sequence", path, name, "cache", desired.cache, current.cache);
}

} // namespace

std::vector<SchemaDiffOperation> SchemaDocumentDiff::operationsByKind(SchemaDiffOperationKind kind) const {
    std::vector<SchemaDiffOperation> result;
    for (const auto& operation : operations) {
        if (operation.kind == kind) {
            result.push_back(operation);
        }
    }
    return result;
}

std::string SchemaDocumentDiff::toText() const {
    std::ostringstream out;
    if (!hasChanges()) {
        out << "No schema differences detected.";
        return out.str();
    }

    out << "Schema differences: " << operations.size() << '\n';
    for (const auto& operation : operations) {
        out << "- " << SchemaDiffEngine::operationKindToString(operation.kind)
            << " " << operation.objectPath;
        if (!operation.property.empty()) {
            out << " property=" << operation.property;
        }
        if (!operation.desiredValue.empty() || !operation.currentValue.empty()) {
            out << " desired='" << operation.desiredValue << "' current='" << operation.currentValue << "'";
        }
        if (!operation.message.empty()) {
            out << " :: " << operation.message;
        }
        out << '\n';
    }
    return out.str();
}

std::string SchemaDocumentDiff::toJsonString() const {
    std::ostringstream out;
    out << "{\"hasChanges\":" << (hasChanges() ? "true" : "false") << ",\"operations\":[";
    for (std::size_t i = 0; i < operations.size(); ++i) {
        const auto& operation = operations[i];
        if (i > 0) {
            out << ',';
        }
        out << '{'
            << "\"kind\":" << quoteJson(SchemaDiffEngine::operationKindToString(operation.kind)) << ','
            << "\"objectType\":" << quoteJson(operation.objectType) << ','
            << "\"objectPath\":" << quoteJson(operation.objectPath) << ','
            << "\"objectName\":" << quoteJson(operation.objectName) << ','
            << "\"property\":" << quoteJson(operation.property) << ','
            << "\"desiredValue\":" << quoteJson(operation.desiredValue) << ','
            << "\"currentValue\":" << quoteJson(operation.currentValue) << ','
            << "\"message\":" << quoteJson(operation.message)
            << '}';
    }
    out << "]}";
    return out.str();
}

SchemaDocumentDiff SchemaDiffEngine::compare(
    const SchemaDocument& desired,
    const SchemaDocument& current,
    const SchemaDiffOptions& options
) {
    SchemaDocument desiredDocument = desired;
    SchemaDocument currentDocument = current;

    if (options.normalizeBeforeCompare) {
        desiredDocument = SchemaNormalizer::normalize(desired, options.normalizationOptions).document;
        currentDocument = SchemaNormalizer::normalize(current, options.normalizationOptions).document;
    }

    SchemaDocumentDiff diff;

    const auto desiredEntities = toMap(desiredDocument.entities(), entityKey);
    const auto currentEntities = toMap(currentDocument.entities(), entityKey);

    for (const auto& item : desiredEntities) {
        const auto currentIt = currentEntities.find(item.first);
        if (currentIt == currentEntities.end()) {
            addOperation(diff, SchemaDiffOperationKind::TableAdded, "table", "/entities/" + item.first, item.first, {}, item.first, {}, "Table exists only in desired schema");
        } else {
            compareEntity(diff, item.second, currentIt->second);
        }
    }
    for (const auto& item : currentEntities) {
        if (desiredEntities.find(item.first) == desiredEntities.end()) {
            addOperation(diff, SchemaDiffOperationKind::TableOnlyInCurrent, "table", "/entities/" + item.first, item.first, {}, {}, item.first, "Table exists only in current database schema");
        }
    }

    const auto desiredViews = toMap(desiredDocument.views(), viewKey);
    const auto currentViews = toMap(currentDocument.views(), viewKey);
    for (const auto& item : desiredViews) {
        const auto currentIt = currentViews.find(item.first);
        if (currentIt == currentViews.end()) {
            addOperation(diff, SchemaDiffOperationKind::ViewAdded, "view", "/views/" + item.first, item.first, {}, item.first, {}, "View exists only in desired schema");
        } else {
            compareView(diff, item.second, currentIt->second, options.compareSqlDefinitions);
        }
    }
    for (const auto& item : currentViews) {
        if (desiredViews.find(item.first) == desiredViews.end()) {
            addOperation(diff, SchemaDiffOperationKind::ViewOnlyInCurrent, "view", "/views/" + item.first, item.first, {}, {}, item.first, "View exists only in current database schema");
        }
    }

    const auto desiredTriggers = toMap(desiredDocument.triggers(), triggerKey);
    const auto currentTriggers = toMap(currentDocument.triggers(), triggerKey);
    for (const auto& item : desiredTriggers) {
        const auto currentIt = currentTriggers.find(item.first);
        if (currentIt == currentTriggers.end()) {
            addOperation(diff, SchemaDiffOperationKind::TriggerAdded, "trigger", "/triggers/" + item.first, item.first, {}, item.first, {}, "Trigger exists only in desired schema");
        } else {
            compareTrigger(diff, item.second, currentIt->second, options.compareSqlDefinitions);
        }
    }
    for (const auto& item : currentTriggers) {
        if (desiredTriggers.find(item.first) == desiredTriggers.end()) {
            addOperation(diff, SchemaDiffOperationKind::TriggerOnlyInCurrent, "trigger", "/triggers/" + item.first, item.first, {}, {}, item.first, "Trigger exists only in current database schema");
        }
    }

    const auto desiredSequences = toMap(desiredDocument.sequences(), sequenceKey);
    const auto currentSequences = toMap(currentDocument.sequences(), sequenceKey);
    for (const auto& item : desiredSequences) {
        const auto currentIt = currentSequences.find(item.first);
        if (currentIt == currentSequences.end()) {
            addOperation(diff, SchemaDiffOperationKind::SequenceAdded, "sequence", "/sequences/" + item.first, item.first, {}, item.first, {}, "Sequence exists only in desired schema");
        } else {
            compareSequence(diff, item.second, currentIt->second);
        }
    }
    for (const auto& item : currentSequences) {
        if (desiredSequences.find(item.first) == desiredSequences.end()) {
            addOperation(diff, SchemaDiffOperationKind::SequenceOnlyInCurrent, "sequence", "/sequences/" + item.first, item.first, {}, {}, item.first, "Sequence exists only in current database schema");
        }
    }

    return diff;
}

const char* SchemaDiffEngine::operationKindToString(SchemaDiffOperationKind kind) {
    switch (kind) {
        case SchemaDiffOperationKind::TableAdded: return "table_added";
        case SchemaDiffOperationKind::TableOnlyInCurrent: return "table_only_in_current";
        case SchemaDiffOperationKind::TableChanged: return "table_changed";
        case SchemaDiffOperationKind::ColumnAdded: return "column_added";
        case SchemaDiffOperationKind::ColumnOnlyInCurrent: return "column_only_in_current";
        case SchemaDiffOperationKind::ColumnChanged: return "column_changed";
        case SchemaDiffOperationKind::PrimaryKeyChanged: return "primary_key_changed";
        case SchemaDiffOperationKind::ForeignKeyAdded: return "foreign_key_added";
        case SchemaDiffOperationKind::ForeignKeyOnlyInCurrent: return "foreign_key_only_in_current";
        case SchemaDiffOperationKind::ForeignKeyChanged: return "foreign_key_changed";
        case SchemaDiffOperationKind::IndexAdded: return "index_added";
        case SchemaDiffOperationKind::IndexOnlyInCurrent: return "index_only_in_current";
        case SchemaDiffOperationKind::IndexChanged: return "index_changed";
        case SchemaDiffOperationKind::ViewAdded: return "view_added";
        case SchemaDiffOperationKind::ViewOnlyInCurrent: return "view_only_in_current";
        case SchemaDiffOperationKind::ViewChanged: return "view_changed";
        case SchemaDiffOperationKind::TriggerAdded: return "trigger_added";
        case SchemaDiffOperationKind::TriggerOnlyInCurrent: return "trigger_only_in_current";
        case SchemaDiffOperationKind::TriggerChanged: return "trigger_changed";
        case SchemaDiffOperationKind::SequenceAdded: return "sequence_added";
        case SchemaDiffOperationKind::SequenceOnlyInCurrent: return "sequence_only_in_current";
        case SchemaDiffOperationKind::SequenceChanged: return "sequence_changed";
    }
    return "unknown";
}
