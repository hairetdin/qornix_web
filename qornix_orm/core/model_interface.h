/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

// model_interface.h
#pragma once
#include <string>
#include <memory>
#include <map>
#include <optional>
#include <vector>
#include "database_interface.h"
#include "entity_base.h"
#include "model_object.h"

class ModelInterface {
private:
    static std::shared_ptr<DatabaseInterface> db_;
    static std::map<std::string, EntityDefinition> modelDefinitions_;

public:
    static void setDb(std::shared_ptr<DatabaseInterface> db);

    // Create data object instance
    std::shared_ptr<ModelObject> getModel(const std::string& entityName);

    // Legacy methods kept for compatibility (optional)
    // std::shared_ptr<EntityBase> get(const std::string& modelName);
    // std::shared_ptr<EntityBase> getEntity(const std::string& entityName);
};
