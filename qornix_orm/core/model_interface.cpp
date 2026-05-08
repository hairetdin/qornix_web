/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

// model_interface.cpp
#include "model_interface.h"
#include "schema_loader.h"
#include <iostream>
#include <sstream>
#include "entity_interface.h"

// Initialize static members
std::shared_ptr<DatabaseInterface> ModelInterface::db_ = nullptr;
std::map<std::string, EntityDefinition> ModelInterface::modelDefinitions_;

void ModelInterface::setDb(std::shared_ptr<DatabaseInterface> db) {
    db_ = std::move(db);
}

std::shared_ptr<ModelObject> ModelInterface::getModel(const std::string& entityName) {
    try {
        // Check whether the entity definition exists
        auto defIt = modelDefinitions_.find(entityName);
        if (defIt == modelDefinitions_.end()) {
            // Load the entity definition from SchemaLoader
            const auto& entities = SchemaLoader::getEntities();
            auto entityIt = entities.find(entityName);

            if (entityIt == entities.end()) {
                std::cerr << "ModelInterface::getModel() - Entity '" << entityName << "' not found in schema" << std::endl;
                return nullptr;
            }

            modelDefinitions_[entityName] = entityIt->second;
        }

        // Create a new data object
        auto dataObject = std::make_shared<ModelObject>(entityName, modelDefinitions_[entityName], db_);
        return dataObject;
    } catch (const std::exception& e) {
        std::cerr << "ModelInterface::getModel() - Error creating model object '" << entityName << "': " << e.what() << std::endl;
        return nullptr;
    }
}

