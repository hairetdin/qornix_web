/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "model_object.h"

#include <iostream>
#include <stdexcept>

#include "database_interface.h"

ModelObject::ModelObject(const std::string& entityName, const EntityDefinition& entityDef,
                       std::shared_ptr<DatabaseInterface> db)
    : entityName_(entityName), entityDef_(entityDef), db_(db) {
    // Initialize attributes with default values from the schema
    for (const auto& field : entityDef_.fields) {
        if (!field.defaultValue.empty()) {
            attributes_[field.name] = field.defaultValue;
        }
    }
    // Also initialize foreign key fields
    for (const auto& fk : entityDef_.foreignKeys) {
        attributes_[fk.name] = "";
    }
}

void ModelObject::setAttribute(const std::string& attributeName, const std::string& value) {
    if (!hasAttribute(attributeName)) {
        throw std::invalid_argument("Attribute '" + attributeName + "' does not exist in entity '" + entityName_ + "'");
    }
    attributes_[attributeName] = value;
}

std::string ModelObject::getAttributeValue(const std::string& attributeName) const {
    auto it = attributes_.find(attributeName);
    if (it != attributes_.end()) {
        return it->second;
    }
    return "";
}

std::map<std::string, std::string> ModelObject::getAllAttributes() const {
    return attributes_;
}

std::map<std::string, std::string> ModelObject::getAllNonEmptyAttributes() const {
    std::map<std::string, std::string> nonEmptyAttributes;
    for (const auto& [key, value] : attributes_) {
        if (!value.empty()) {  // Only non-empty values
            nonEmptyAttributes[key] = value;
        }
    }
    return nonEmptyAttributes;
}

bool ModelObject::hasAttribute(const std::string& attributeName) const {
    // Check in attributes map
    if (attributes_.find(attributeName) != attributes_.end()) {
        return true;
    }

    // Check in entity fields
    for (const auto& field : entityDef_.fields) {
        if (field.name == attributeName) {
            return true;
        }
    }

    // Check in foreign key fields
    for (const auto& fk : entityDef_.foreignKeys) {
        if (fk.name == attributeName) {
            return true;
        }
    }

    return false;
}

std::string ModelObject::getPrimaryKeyField() const {
    for (const auto& field : entityDef_.fields) {
        if (field.primaryKey) {
            return field.name;
        }
    }
    return "id";
}

// Proxy method to get TableManager for this entity
ModelTableProxy ModelObject::objects() {
    if (!db_) {
        throw std::runtime_error("Database interface is not set");
    }

    auto tableManager = db_->table(getTableName());
    return ModelTableProxy(tableManager, db_, entityDef_);
}

void ModelObject::updateAttributesFromObject(const std::shared_ptr<ModelObject>& sourceModel) {
    // ModelTableProxy creates its own model instance, so
    // update the current object attributes with data from the response objects() result.
    auto sourceAttributes = sourceModel->getAllAttributes();
    for (const auto& [attrName, attrValue] : sourceAttributes) {
        if (hasAttribute(attrName)) {
            attributes_[attrName] = attrValue;
        }
    }
}

// Direct operations on this specific object
bool ModelObject::save() {
    if (!db_) {
        throw std::runtime_error("Database interface is not set");
    }

    std::string pkField = getPrimaryKeyField();
    std::string id = getAttributeValue(pkField);

    if (id.empty() || id == "" || id == "0" || id == "NULL") {
        // New record - create
        auto attributes = getAllNonEmptyAttributes();
        attributes.erase(pkField);
        auto result = objects().create(attributes);
        updateAttributesFromObject(result);
        return true;
    } else {
        // Existing record - update
        auto nonEmptyAttributes = getAllNonEmptyAttributes();
        // Remove primary key from update data
        nonEmptyAttributes.erase(pkField);

        std::string condition = pkField + "=" + id;
        auto updated = objects().filter(condition).update(nonEmptyAttributes);

        // If the update succeeds, update the attributes of the current object
        if (updated > 0) {
            // Get the updated data from the database
            auto updatedModel = objects().get(condition);
            updateAttributesFromObject(updatedModel);
        }

        return updated > 0;
    }
}

ModelObject ModelObject::get(const std::string &condition) {
    if (!db_) {
        throw std::runtime_error("Database interface is not set");
    }

    auto result = objects().get(condition);
    updateAttributesFromObject(result);
    return *this;
}

ModelObject ModelObject::create(const std::map<std::string, std::string>& data) {
    auto result = objects().create(data);
    updateAttributesFromObject(result);
    return *this;
}

bool ModelObject::update(const std::string& condition) {
    if (!db_) {
        throw std::runtime_error("Database interface is not set");
    }

    auto nonEmptyAttributes = getAllNonEmptyAttributes();
    std::string pkField = getPrimaryKeyField();
    nonEmptyAttributes.erase(pkField); // Remove primary key from update

    auto updated = objects().filter(condition).update(nonEmptyAttributes);
    if (updated > 0) {
        auto updatedModel = objects().get(condition);
        updateAttributesFromObject(updatedModel);
    }
    return updated > 0;
}

bool ModelObject::remove(const std::string& condition) {
    if (!db_) {
        throw std::runtime_error("Database interface is not set");
    }

    auto deleted = objects().filter(condition).remove();
    return deleted > 0;
}
