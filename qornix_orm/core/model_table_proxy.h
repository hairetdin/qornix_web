/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include <memory>
#include <vector>
#include <string>
#include "database_interface.h"

class ModelObject;

class ModelTableProxy {
private:
    TableManager tableManager_;
    std::shared_ptr<DatabaseInterface> db_;
    EntityDefinition entityDef_;

public:
    ModelTableProxy(TableManager tableManager,
                   std::shared_ptr<DatabaseInterface> db,
                   EntityDefinition entityDef);

    // Methods for returning model objects
    std::vector<std::shared_ptr<ModelObject>> all();
    std::vector<std::shared_ptr<ModelObject>> execute();

    ModelTableProxy filter(const std::string &condition);

    ModelTableProxy filter(const std::map<std::string, std::string> &conditions);
    std::shared_ptr<ModelObject> get(const std::string& condition);
    std::shared_ptr<ModelObject> get(const std::map<std::string, std::string>& conditions);

    // Proxy methods for other operations
    ModelTableProxy& limit(int limit_value);
    ModelTableProxy& order_by(const std::string& ordering);
    ModelTableProxy& group_by(const std::string& grouping);
    ModelTableProxy& having(const std::string& condition);
    ModelTableProxy& join(const std::string& table, const std::string& on_condition);

    // CRUD operations
    std::shared_ptr<ModelObject> create(const std::map<std::string, std::string>& data);
    int update(const std::map<std::string, std::string>& data);
    int remove();
    int delete_();

private:
    // Helper method for converting results to model objects
    std::vector<std::shared_ptr<ModelObject>> convertToModelObjects(
        const std::vector<std::map<std::string, std::string>>& rawData);

    std::shared_ptr<ModelObject> convertToModelObject(
        const std::map<std::string, std::string>& rawData);
};
