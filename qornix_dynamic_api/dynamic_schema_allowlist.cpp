/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "dynamic_schema_allowlist.h"

#include <algorithm>
#include <utility>

namespace qornix_dynamic_api {

namespace {
std::string tableNameForEntity(const EntityDefinition& entity) {
    return entity.tableName.empty() ? entity.name : entity.tableName;
}

DynamicFieldPolicy fieldPolicyFromField(const FieldDefinition& field) {
    DynamicFieldPolicy policy;
    policy.name = field.name;
    policy.type = field.type;
    policy.readable = true;
    policy.writable = !field.primaryKey && !field.autoIncrement && !field.isComputed;
    policy.filterable = !field.name.empty();
    policy.sortable = !field.name.empty();
    policy.hidden = false;
    return policy;
}
}

DynamicSchemaAllowlist DynamicSchemaAllowlist::fromSchemaDocument(const SchemaDocument& document) {
    DynamicSchemaAllowlist allowlist;
    for (const auto& entity : document.entities()) {
        DynamicTablePolicy table;
        table.name = tableNameForEntity(entity);
        table.entityName = entity.name;
        for (const auto& field : entity.fields) {
            if (field.name.empty()) {
                continue;
            }
            table.fields[field.name] = fieldPolicyFromField(field);
        }
        for (const auto& foreignKey : entity.foreignKeys) {
            if (foreignKey.name.empty() || table.fields.count(foreignKey.name)) {
                continue;
            }
            table.fields[foreignKey.name] = fieldPolicyFromField(foreignKey);
        }
        if (!table.name.empty()) {
            allowlist.tables_[table.name] = table;
        }
    }
    return allowlist;
}

DynamicSchemaAllowlist DynamicSchemaAllowlist::fromDatabase(DatabaseInterface& database) {
    DynamicSchemaAllowlist allowlist;
    for (const auto& tableName : database.getTableNames()) {
        DynamicTablePolicy table;
        table.name = tableName;
        table.entityName = tableName;
        for (const auto& fieldName : database.getTableFields(tableName)) {
            DynamicFieldPolicy field;
            field.name = fieldName;
            field.type = "string";
            field.readable = true;
            field.writable = fieldName != "id";
            field.filterable = true;
            field.sortable = true;
            table.fields[fieldName] = field;
        }
        allowlist.tables_[tableName] = table;
    }
    return allowlist;
}

void DynamicSchemaAllowlist::addTable(DynamicTablePolicy policy) {
    if (policy.name.empty()) {
        return;
    }
    tables_[policy.name] = std::move(policy);
}

bool DynamicSchemaAllowlist::hasTable(const std::string& tableName) const {
    return tables_.find(tableName) != tables_.end();
}

bool DynamicSchemaAllowlist::hasField(const std::string& tableName, const std::string& fieldName) const {
    auto tableIt = tables_.find(tableName);
    if (tableIt == tables_.end()) {
        return false;
    }
    return tableIt->second.fields.find(fieldName) != tableIt->second.fields.end();
}

bool DynamicSchemaAllowlist::canReadTable(const std::string& tableName) const {
    auto it = tables_.find(tableName);
    return it != tables_.end() && it->second.readable;
}

bool DynamicSchemaAllowlist::canWriteTable(const std::string& tableName) const {
    auto it = tables_.find(tableName);
    return it != tables_.end() && it->second.writable;
}

bool DynamicSchemaAllowlist::canDeleteFromTable(const std::string& tableName) const {
    auto it = tables_.find(tableName);
    return it != tables_.end() && it->second.deletable;
}

bool DynamicSchemaAllowlist::canReadField(const std::string& tableName, const std::string& fieldName) const {
    auto tableIt = tables_.find(tableName);
    if (tableIt == tables_.end()) return false;
    auto fieldIt = tableIt->second.fields.find(fieldName);
    return fieldIt != tableIt->second.fields.end() && fieldIt->second.readable && !fieldIt->second.hidden;
}

bool DynamicSchemaAllowlist::canWriteField(const std::string& tableName, const std::string& fieldName) const {
    auto tableIt = tables_.find(tableName);
    if (tableIt == tables_.end()) return false;
    auto fieldIt = tableIt->second.fields.find(fieldName);
    return fieldIt != tableIt->second.fields.end() && fieldIt->second.writable && !fieldIt->second.hidden;
}

bool DynamicSchemaAllowlist::canFilterField(const std::string& tableName, const std::string& fieldName) const {
    auto tableIt = tables_.find(tableName);
    if (tableIt == tables_.end()) return false;
    auto fieldIt = tableIt->second.fields.find(fieldName);
    return fieldIt != tableIt->second.fields.end() && fieldIt->second.filterable && !fieldIt->second.hidden;
}

bool DynamicSchemaAllowlist::canSortField(const std::string& tableName, const std::string& fieldName) const {
    auto tableIt = tables_.find(tableName);
    if (tableIt == tables_.end()) return false;
    auto fieldIt = tableIt->second.fields.find(fieldName);
    return fieldIt != tableIt->second.fields.end() && fieldIt->second.sortable && !fieldIt->second.hidden;
}

std::vector<std::string> DynamicSchemaAllowlist::tables() const {
    std::vector<std::string> result;
    for (const auto& [name, _] : tables_) {
        result.push_back(name);
    }
    return result;
}

std::vector<std::string> DynamicSchemaAllowlist::fieldsForTable(const std::string& tableName) const {
    std::vector<std::string> result;
    auto tableIt = tables_.find(tableName);
    if (tableIt == tables_.end()) {
        return result;
    }
    for (const auto& [name, _] : tableIt->second.fields) {
        result.push_back(name);
    }
    return result;
}

boost::json::object DynamicSchemaAllowlist::toJson() const {
    boost::json::object root;
    boost::json::array tables;
    for (const auto& [tableName, table] : tables_) {
        boost::json::object tableObj;
        tableObj["name"] = tableName;
        tableObj["entity"] = table.entityName;
        tableObj["readable"] = table.readable;
        tableObj["writable"] = table.writable;
        tableObj["deletable"] = table.deletable;

        boost::json::array fields;
        for (const auto& [fieldName, field] : table.fields) {
            boost::json::object fieldObj;
            fieldObj["name"] = fieldName;
            fieldObj["type"] = field.type;
            fieldObj["readable"] = field.readable;
            fieldObj["writable"] = field.writable;
            fieldObj["filterable"] = field.filterable;
            fieldObj["sortable"] = field.sortable;
            fieldObj["hidden"] = field.hidden;
            fields.emplace_back(fieldObj);
        }
        tableObj["fields"] = fields;
        tables.emplace_back(tableObj);
    }
    root["tables"] = tables;
    return root;
}

} // namespace qornix_dynamic_api
