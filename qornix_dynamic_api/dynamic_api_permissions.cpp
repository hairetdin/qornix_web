/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "dynamic_api_permissions.h"

#include <algorithm>

namespace qornix_dynamic_api {

DynamicPermissionPolicy::DynamicPermissionPolicy(DynamicApiConfig config)
    : config_(std::move(config)) {}

bool DynamicPermissionPolicy::tokenAllowed(const std::string& token) const {
    if (token.empty()) {
        return false;
    }
    return std::find(config_.adminTokens.begin(), config_.adminTokens.end(), token) != config_.adminTokens.end();
}

DynamicPermissionResult DynamicPermissionPolicy::check(
    const std::string& permission,
    const http::request<http::string_body>& request
) const {
    DynamicPermissionResult result;
    result.permission = permission;

    if (!config_.requirePermissions) {
        result.allowed = true;
        result.message = "permission checks disabled";
        return result;
    }

    std::string token;
    if (request.find("X-Qornix-Admin-Token") != request.end()) {
        token = std::string(request.at("X-Qornix-Admin-Token"));
    } else if (request.find(http::field::authorization) != request.end()) {
        const std::string authorization = std::string(request.at(http::field::authorization));
        const std::string prefix = "Bearer ";
        if (authorization.rfind(prefix, 0) == 0) {
            token = authorization.substr(prefix.size());
        }
    }

    result.allowed = tokenAllowed(token);
    result.message = result.allowed
        ? "permission granted"
        : "permission denied for " + permission;
    return result;
}

std::string DynamicPermissionPolicy::permissionForSchemaValidate() { return "schema.validate"; }
std::string DynamicPermissionPolicy::permissionForSchemaDiff() { return "schema.diff"; }
std::string DynamicPermissionPolicy::permissionForSchemaPlan() { return "schema.plan"; }
std::string DynamicPermissionPolicy::permissionForSchemaApply(bool destructive) { return destructive ? "schema.apply_destructive" : "schema.apply_safe"; }
std::string DynamicPermissionPolicy::permissionForSchemaHistory() { return "schema.history"; }
std::string DynamicPermissionPolicy::permissionForSchemaExport() { return "schema.export"; }
std::string DynamicPermissionPolicy::permissionForDynamicRead() { return "dynamic.read"; }
std::string DynamicPermissionPolicy::permissionForDynamicWrite() { return "dynamic.write"; }
std::string DynamicPermissionPolicy::permissionForDynamicDelete() { return "dynamic.delete"; }

std::string DynamicPermissionPolicy::permissionForOpenApiRead() { return "openapi.read"; }

} // namespace qornix_dynamic_api
