/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include <string>
#include <map>
#include <memory>
#include "app_struct.h"
#include "database_interface.h"
#include "model_table_proxy.h"

class ModelObject {
private:
    std::string entityName_;
    std::map<std::string, std::string> attributes_;
    EntityDefinition entityDef_;
    std::shared_ptr<DatabaseInterface> db_;
    void updateAttributesFromObject(const std::shared_ptr<ModelObject>& sourceModel);

public:
    ModelObject(const std::string& entityName, const EntityDefinition& entityDef,
               std::shared_ptr<DatabaseInterface> db);

    // Methods for working with attributes
    void setAttribute(const std::string& attributeName, const std::string& value);
    std::string getAttributeValue(const std::string& attributeName) const;
    std::map<std::string, std::string> getAllAttributes() const;

    std::map<std::string, std::string> getAllNonEmptyAttributes() const;

    // Methods for getting entity information
    std::string getEntityName() const { return entityName_; }
    const EntityDefinition& getEntityDefinition() const { return entityDef_; }

    // Check for attribute existence
    bool hasAttribute(const std::string& attributeName) const;

    std::string getPrimaryKeyField() const;

    // Get entity table name
    std::string getTableName() const { return entityDef_.tableName; }

    // Proxy for table operations - returns TableManager for this entity
    ModelTableProxy objects();

    // Direct operations on this specific object
    bool save();  // Save current object
    ModelObject get(const std::string& condition);  // Load into current object
    ModelObject create(const std::map<std::string, std::string> &data);
    bool update(const std::string& condition);  // Update current object
    bool remove(const std::string& condition);  // Delete current object
};
