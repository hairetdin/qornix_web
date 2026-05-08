/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <map>

#include "database_interface.h"
#include "entity_base.h"

class EntityInterface {
private:
    static std::map<std::string, std::shared_ptr<EntityBase>> entityInstances_;

public:
    static std::shared_ptr<DatabaseInterface> db;
    static void init(std::shared_ptr<DatabaseInterface> db);
    static void loadAllEntities();
    static void reloadEntities(std::shared_ptr<DatabaseInterface> _db);

    static const std::map<std::string, std::shared_ptr<EntityBase>>& getAllEntities();

    static std::vector<std::string> getAllEntityNames();

    static std::shared_ptr<EntityBase> newEntity(const std::string& entityName);
    static std::shared_ptr<EntityBase> getEntity(const std::string& entityName);

    static bool deleteEntity(const std::string& entityName);
    static bool removeEntity(const std::string& entityName) {
        return deleteEntity(entityName);
    }

    // Method for registering a new entity in the system
    static void registerNewEntity(const std::string& entityName,
                                  const EntityDefinition& entityDef);
};

