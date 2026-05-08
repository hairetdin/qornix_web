/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "model_table_proxy.h"
#include <algorithm>
#include <database_interface.h>
#include <iostream>

#include "model_object.h"

ModelTableProxy::ModelTableProxy(TableManager tableManager,
                                 std::shared_ptr<DatabaseInterface> db,
                                 EntityDefinition entityDef)
    : tableManager_(tableManager), db_(db), entityDef_(entityDef) {
}

std::vector<std::shared_ptr<ModelObject>> ModelTableProxy::all() {
    auto rawData = tableManager_.all();
    return convertToModelObjects(rawData);
}

std::vector<std::shared_ptr<ModelObject>> ModelTableProxy::execute() {
    auto rawData = tableManager_.execute();
    return convertToModelObjects(rawData);
}

std::shared_ptr<ModelObject> ModelTableProxy::get(const std::string& condition) {
    auto rawData = tableManager_.get(condition);
    return convertToModelObject(rawData);
}

std::shared_ptr<ModelObject> ModelTableProxy::get(const std::map<std::string, std::string>& conditions) {
    auto rawData = tableManager_.get(conditions);
    return convertToModelObject(rawData);
}

ModelTableProxy ModelTableProxy::filter(const std::string &condition) {
    tableManager_ = tableManager_.filter(condition);
    return *this;
}

ModelTableProxy ModelTableProxy::filter(const std::map<std::string, std::string> &conditions) {
    tableManager_ = tableManager_.filter(conditions);
    return *this;
}

ModelTableProxy& ModelTableProxy::limit(int limit_value) {
    tableManager_ = tableManager_.limit(limit_value);
    return *this;
}

ModelTableProxy& ModelTableProxy::order_by(const std::string& ordering) {
    tableManager_ = tableManager_.order_by(ordering);
    return *this;
}

ModelTableProxy& ModelTableProxy::group_by(const std::string& grouping) {
    tableManager_ = tableManager_.group_by(grouping);
    return *this;
}

ModelTableProxy& ModelTableProxy::having(const std::string& condition) {
    tableManager_ = tableManager_.having(condition);
    return *this;
}

ModelTableProxy& ModelTableProxy::join(const std::string& table, const std::string& on_condition) {
    tableManager_ = tableManager_.join(table, on_condition);
    return *this;
}

std::shared_ptr<ModelObject> ModelTableProxy::create(const std::map<std::string, std::string>& data) {
    auto result = tableManager_.create(data);
    return convertToModelObject(result);
}

int ModelTableProxy::update(const std::map<std::string, std::string>& data) {
    return tableManager_.update(data);
}

int ModelTableProxy::remove() {
    return tableManager_.remove();
}

int ModelTableProxy::delete_() {
    return tableManager_.delete_();
}

std::vector<std::shared_ptr<ModelObject>> ModelTableProxy::convertToModelObjects(
    const std::vector<std::map<std::string, std::string>>& rawData) {
    std::vector<std::shared_ptr<ModelObject>> modelObjects;

    for (const auto& row : rawData) {
        auto modelObj = std::make_shared<ModelObject>(entityDef_.name, entityDef_, db_);
        for (const auto& [key, value] : row) {
            if (modelObj->hasAttribute(key)) {
                modelObj->setAttribute(key, value);
            }
        }
        modelObjects.push_back(modelObj);
    }

    return modelObjects;
}

std::shared_ptr<ModelObject> ModelTableProxy::convertToModelObject(
    const std::map<std::string, std::string>& rawData) {
    auto modelObj = std::make_shared<ModelObject>(entityDef_.name, entityDef_, db_);
    // std::cout << "DEBUG: Converting query result to model object" << std::endl;
    for (const auto& [key, value] : rawData) {
        if (modelObj->hasAttribute(key)) {
            // std::cout  << "DEBUG: Setting model attribute " << key << " to " << value << std::endl;
            modelObj->setAttribute(key, value);
        }
    }
    return modelObj;
}
