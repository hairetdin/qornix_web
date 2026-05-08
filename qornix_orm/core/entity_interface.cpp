/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "entity_interface.h"
#include "schema_loader.h"
#include <iostream>

// Define static members
std::map<std::string, std::shared_ptr<EntityBase>> EntityInterface::entityInstances_;
std::shared_ptr<DatabaseInterface> EntityInterface::db;

void EntityInterface::init(std::shared_ptr<DatabaseInterface> db_) {
    db = std::move(db_);
    loadAllEntities();
}

std::shared_ptr<EntityBase> EntityInterface::newEntity(const std::string& entityName) {
    if (!db) {
        std::cerr << "EntityInterface::newEntity() - Database interface is null" << std::endl;
        return nullptr;
    }

    // Create a new entity through EntityBase
    EntityDefinition entityDef = EntityBase::newEntity(entityName);
    auto entityBase = std::make_shared<EntityBase>(db, entityDef);

    // Register the entity in the system
    registerNewEntity(entityName, entityDef);

    return entityBase;
}

bool EntityInterface::deleteEntity(const std::string& entityName) {
    auto entity = getEntity(entityName);
    if (entity) {
        bool result = entity->deleteEntity();
        // Remove from the interface cache
        entityInstances_.erase(entityName);
        // Remove from the database schema
        auto& dbEntities = const_cast<std::map<std::string, EntityDefinition>&>(SchemaLoader::getDbEntities());
        dbEntities.erase(entityName);

        return result;
    }
    return false;
}

void EntityInterface::registerNewEntity(const std::string& entityName,
                                        const EntityDefinition& entityDef) {
    // Add to the local cache
    auto entityBase = std::make_shared<EntityBase>(db, entityDef);
    entityInstances_[entityName] = entityBase;

    // Synchronize with SchemaLoader
    SchemaLoader::entities_[entityName] = entityDef;
    // SchemaLoader::dbEntities_[entityName] = entityDef;

    std::cout << "Schema registered new entity: " << entityName << std::endl;
}


std::shared_ptr<EntityBase> EntityInterface::getEntity(const std::string& entityName) {
    auto it = entityInstances_.find(entityName);
    if (it != entityInstances_.end()) {
        return it->second;
    }
    std::cerr << "Error: Entity '" << entityName << "' not found" << std::endl;
    std::cerr << "Available entities: ";
    auto entityNames = getAllEntityNames();
    for (size_t i = 0; i < entityNames.size(); ++i) {
        std::cerr << entityNames[i];
        if (i < entityNames.size() - 1) {
            std::cerr << ", ";
        }
    }
    std::cerr << std::endl;
    return nullptr;
}

std::vector<std::string> EntityInterface::getAllEntityNames() {
    std::vector<std::string> entityNames;
    for (const auto& pair : entityInstances_) {
        entityNames.push_back(pair.first);
    }
    return entityNames;
}

void EntityInterface::loadAllEntities() {
    const auto& entities = SchemaLoader::getEntities();
    for (const auto& pair : entities) {
        const std::string& entityName = pair.first;
        const EntityDefinition& entityDef = pair.second;

        if (!db) {
            std::cerr << "EntityInterface::loadAllEntities() - Database interface is null" << std::endl;
            continue;
        }

        auto entity = std::make_shared<EntityBase>(db, entityDef);
        entityInstances_[entityName] = entity;
    }
}

void EntityInterface::reloadEntities(std::shared_ptr<DatabaseInterface> _db) {
    db = std::move(_db);
    entityInstances_.clear();  // Clear old instances
    loadAllEntities();         // Load everything again
}

const std::map<std::string, std::shared_ptr<EntityBase>>& EntityInterface::getAllEntities() {
    return entityInstances_;
}
