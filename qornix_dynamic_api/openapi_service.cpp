/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "openapi_service.h"

namespace qornix_dynamic_api {

DynamicOpenApiService::DynamicOpenApiService(DynamicApiConfig config)
    : config_(std::move(config)) {}

DynamicApiResponse DynamicOpenApiService::openApiJson() const {
    try {
        auto db = DatabaseInterface::init(config_.databaseConfig);
        auto allowlist = DynamicSchemaAllowlist::fromDatabase(*db);

        boost::json::object root;
        root["openapi"] = "3.0.3";
        boost::json::object info;
        info["title"] = "Qornix Schema-driven Dynamic API";
        info["version"] = "0.1.0";
        info["description"] = "Dynamic CRUD/query API generated from qornix_orm schema metadata.";
        root["info"] = info;

        boost::json::object paths;

        for (const auto& table : allowlist.tables()) {
            boost::json::object collection;
            boost::json::object getCollection;
            getCollection["summary"] = "List " + table;
            getCollection["tags"] = boost::json::array{table};
            getCollection["responses"] = boost::json::object{{"200", boost::json::object{{"description", "OK"}}}};
            collection["get"] = getCollection;

            boost::json::object postCollection;
            postCollection["summary"] = "Create " + table;
            postCollection["tags"] = boost::json::array{table};
            postCollection["responses"] = boost::json::object{{"200", boost::json::object{{"description", "Created"}}}};
            collection["post"] = postCollection;
            paths[config_.apiPrefix + "/" + table] = collection;

            boost::json::object item;
            boost::json::array params;
            boost::json::object idParam;
            idParam["name"] = "id";
            idParam["in"] = "path";
            idParam["required"] = true;
            idParam["schema"] = boost::json::object{{"type", "string"}};
            params.emplace_back(idParam);

            boost::json::object getItem;
            getItem["summary"] = "Get " + table + " by id";
            getItem["tags"] = boost::json::array{table};
            getItem["parameters"] = params;
            getItem["responses"] = boost::json::object{{"200", boost::json::object{{"description", "OK"}}}};
            item["get"] = getItem;

            boost::json::object putItem = getItem;
            putItem["summary"] = "Update " + table + " by id";
            item["put"] = putItem;

            boost::json::object patchItem = getItem;
            patchItem["summary"] = "Patch " + table + " by id";
            item["patch"] = patchItem;

            boost::json::object deleteItem = getItem;
            deleteItem["summary"] = "Delete " + table + " by id";
            item["delete"] = deleteItem;

            paths[config_.apiPrefix + "/" + table + "/{id}"] = item;
        }

        boost::json::object schemaValidate;
        boost::json::object postValidate;
        postValidate["summary"] = "Validate XML schema";
        postValidate["tags"] = boost::json::array{"Schema Manager"};
        postValidate["responses"] = boost::json::object{{"200", boost::json::object{{"description", "Valid"}}}, {"400", boost::json::object{{"description", "Invalid"}}}};
        schemaValidate["post"] = postValidate;
        paths[config_.schemaPrefix + "/validate"] = schemaValidate;

        boost::json::object schemaDiff;
        boost::json::object postDiff = postValidate;
        postDiff["summary"] = "Build schema diff";
        schemaDiff["post"] = postDiff;
        paths[config_.schemaPrefix + "/diff"] = schemaDiff;

        boost::json::object schemaPlan;
        boost::json::object postPlan = postValidate;
        postPlan["summary"] = "Build schema plan and SQL preview";
        schemaPlan["post"] = postPlan;
        paths[config_.schemaPrefix + "/plan"] = schemaPlan;

        boost::json::object schemaApply;
        boost::json::object postApply = postValidate;
        postApply["summary"] = "Apply confirmed schema plan";
        schemaApply["post"] = postApply;
        paths[config_.schemaPrefix + "/apply"] = schemaApply;


        boost::json::object queryPath;
        boost::json::object postQuery;
        postQuery["summary"] = "Execute structured dynamic query";
        postQuery["tags"] = boost::json::array{"Dynamic Query"};
        postQuery["responses"] = boost::json::object{{"200", boost::json::object{{"description", "OK"}}}, {"400", boost::json::object{{"description", "Invalid query"}}}};
        queryPath["post"] = postQuery;
        paths[config_.apiPrefix + "/query"] = queryPath;

        boost::json::object historyPath;
        boost::json::object getHistory;
        getHistory["summary"] = "List schema plan/apply history";
        getHistory["tags"] = boost::json::array{"Schema Manager"};
        getHistory["responses"] = boost::json::object{{"200", boost::json::object{{"description", "OK"}}}};
        historyPath["get"] = getHistory;
        paths[config_.schemaPrefix + "/history"] = historyPath;

        root["paths"] = paths;
        return DynamicApiResponse::ok(root, "OpenAPI generated");
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "openapi_failed", e.what());
    }
}

} // namespace qornix_dynamic_api
