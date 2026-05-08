/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include "dynamic_api_config.h"

#include <boost/beast/http.hpp>
#include <map>
#include <string>

namespace qornix_dynamic_api {

namespace http = boost::beast::http;

struct DynamicPermissionResult {
    bool allowed = true;
    std::string permission;
    std::string message;
};

class DynamicPermissionPolicy {
public:
    explicit DynamicPermissionPolicy(DynamicApiConfig config = {});

    DynamicPermissionResult check(
        const std::string& permission,
        const http::request<http::string_body>& request
    ) const;

    static std::string permissionForSchemaValidate();
    static std::string permissionForSchemaDiff();
    static std::string permissionForSchemaPlan();
    static std::string permissionForSchemaApply(bool destructive);
    static std::string permissionForSchemaHistory();
    static std::string permissionForSchemaExport();
    static std::string permissionForDynamicRead();
    static std::string permissionForDynamicWrite();
    static std::string permissionForDynamicDelete();
    static std::string permissionForOpenApiRead();

private:
    DynamicApiConfig config_;
    bool tokenAllowed(const std::string& token) const;
};

} // namespace qornix_dynamic_api
