/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include "database_interface.h"
#include "schema_document.h"

#include <boost/json.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace qornix_dynamic_api {

struct DynamicFieldPolicy {
    std::string name;
    std::string type;
    bool readable = true;
    bool writable = true;
    bool filterable = true;
    bool sortable = true;
    bool hidden = false;
};

struct DynamicTablePolicy {
    std::string name;
    std::string entityName;
    bool readable = true;
    bool writable = true;
    bool deletable = true;
    std::map<std::string, DynamicFieldPolicy> fields;
};

class DynamicSchemaAllowlist {
public:
    static DynamicSchemaAllowlist fromSchemaDocument(const SchemaDocument& document);
    static DynamicSchemaAllowlist fromDatabase(DatabaseInterface& database);

    bool hasTable(const std::string& tableName) const;
    bool hasField(const std::string& tableName, const std::string& fieldName) const;
    bool canReadTable(const std::string& tableName) const;
    bool canWriteTable(const std::string& tableName) const;
    bool canDeleteFromTable(const std::string& tableName) const;
    bool canReadField(const std::string& tableName, const std::string& fieldName) const;
    bool canWriteField(const std::string& tableName, const std::string& fieldName) const;
    bool canFilterField(const std::string& tableName, const std::string& fieldName) const;
    bool canSortField(const std::string& tableName, const std::string& fieldName) const;

    std::vector<std::string> tables() const;
    std::vector<std::string> fieldsForTable(const std::string& tableName) const;

    boost::json::object toJson() const;

private:
    std::map<std::string, DynamicTablePolicy> tables_;
};

} // namespace qornix_dynamic_api
