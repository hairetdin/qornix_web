/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include "dynamic_api_config.h"
#include "dynamic_api_permissions.h"
#include "dynamic_query_service.h"
#include "openapi_service.h"
#include "schema_service.h"

#include "handler_base.h"
#include "http_server.h"

#include <boost/json.hpp>
#include <map>
#include <memory>
#include <string>

#ifndef QORNIX_ENABLE_ASYNC_DB
#define QORNIX_ENABLE_ASYNC_DB 0
#endif

#if QORNIX_ENABLE_ASYNC_DB
class AsyncDatabaseInterface;
#endif

namespace qornix_dynamic_api {

class DynamicApiHandlerBase : public HandlerBase {
public:
    explicit DynamicApiHandlerBase(DynamicApiConfig config);

protected:
    DynamicApiConfig config_;
    DynamicPermissionPolicy permissionPolicy_;

    void sendApiResponse(http::response<http::string_body>& res, const DynamicApiResponse& response) const;
    bool requirePermission(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const std::string& permission
    ) const;

    static std::string getQueryParam(const urls::url_view& url, const std::string& key);
    static int getQueryIntParam(const urls::url_view& url, const std::string& key, int defaultValue, int minValue, int maxValue);
    static std::string xmlFromRequestBody(const std::string& body);
};

class DynamicCrudHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;

protected:
    void handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
    void handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
    void handlePut(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
    void handlePatch(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
    void handleDelete(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;

private:
    void executeMethod(const std::string& method, const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params);
};

class DynamicMetadataHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;
protected:
    void handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
};

class DynamicSchemaExportHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;
protected:
    void handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
};

class DynamicSchemaValidateHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;
protected:
    void handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
};

class DynamicSchemaDiffHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;
protected:
    void handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
};

class DynamicSchemaPlanHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;
protected:
    void handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
};

class DynamicSchemaApplyHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;
protected:
    void handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
};

class DynamicSchemaHistoryHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;
protected:
    void handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
};

class DynamicOpenApiHandler : public DynamicApiHandlerBase {
public:
    using DynamicApiHandlerBase::DynamicApiHandlerBase;
protected:
    void handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) override;
};

void addSchemaDrivenDynamicApiRoutes(HttpServer& server, const DynamicApiConfig& config);

#if QORNIX_ENABLE_ASYNC_DB
void addSchemaDrivenDynamicApiAsyncRoutes(
    HttpServer& server,
    const DynamicApiConfig& config,
    std::shared_ptr<AsyncDatabaseInterface> asyncDatabase,
    DynamicSchemaAllowlist allowlist);
#endif

} // namespace qornix_dynamic_api
