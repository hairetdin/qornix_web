/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "dynamic_api_handlers.h"

#include <algorithm>
#include <climits>

namespace qornix_dynamic_api {

DynamicApiHandlerBase::DynamicApiHandlerBase(DynamicApiConfig config)
    : config_(std::move(config)), permissionPolicy_(config_) {}

void DynamicApiHandlerBase::sendApiResponse(http::response<http::string_body>& res, const DynamicApiResponse& response) const {
    res.result(response.status);
    res.set(http::field::content_type, "application/json; charset=utf-8");
    if (response.body.is_string()) {
        res.body() = response.body.as_string().c_str();
    } else {
        res.body() = response.toJsonString();
    }
    res.prepare_payload();
}

bool DynamicApiHandlerBase::requirePermission(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res,
    const std::string& permission
) const {
    auto decision = permissionPolicy_.check(permission, req);
    if (decision.allowed) {
        return true;
    }
    sendApiResponse(res, DynamicApiResponse::error(http::status::forbidden, "permission_denied", decision.message));
    return false;
}

std::string DynamicApiHandlerBase::getQueryParam(const urls::url_view& url, const std::string& key) {
    for (const auto& param : url.params()) {
        if (std::string(param.key) == key) {
            return std::string(param.value);
        }
    }
    return "";
}

int DynamicApiHandlerBase::getQueryIntParam(const urls::url_view& url, const std::string& key, int defaultValue, int minValue, int maxValue) {
    const std::string raw = getQueryParam(url, key);
    if (raw.empty()) return defaultValue;
    try {
        long long parsed = std::stoll(raw);
        if (parsed < minValue) return minValue;
        if (parsed > maxValue) return maxValue;
        return static_cast<int>(parsed);
    } catch (...) {
        return defaultValue;
    }
}

std::string DynamicApiHandlerBase::xmlFromRequestBody(const std::string& body) {
    std::string trimmed = body;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    if (!trimmed.empty() && trimmed.front() == '<') {
        return body;
    }
    try {
        auto parsed = boost::json::parse(body);
        if (parsed.is_object()) {
            const auto& obj = parsed.as_object();
            auto it = obj.find("xml");
            if (it != obj.end() && it->value().is_string()) {
                return it->value().as_string().c_str();
            }
        }
    } catch (...) {
    }
    return body;
}

void DynamicCrudHandler::executeMethod(const std::string& method, const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) {
    std::string permission = DynamicPermissionPolicy::permissionForDynamicRead();
    if (method == "POST" || method == "PUT" || method == "PATCH") permission = DynamicPermissionPolicy::permissionForDynamicWrite();
    if (method == "DELETE") permission = DynamicPermissionPolicy::permissionForDynamicDelete();
    if (!requirePermission(req, res, permission)) return;

    DynamicQueryRequest request;
    request.method = method;
    request.table = params.count("table") ? params.at("table") : "";
    request.id = params.count("id") ? params.at("id") : "";
    request.rawBody = req.body();
    request.rawQuery = std::string(url.query());

    DynamicQueryService service(config_);
    sendApiResponse(res, service.execute(request));
}

void DynamicCrudHandler::handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) { executeMethod("GET", req, res, url, params); }
void DynamicCrudHandler::handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) { executeMethod("POST", req, res, url, params); }
void DynamicCrudHandler::handlePut(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) { executeMethod("PUT", req, res, url, params); }
void DynamicCrudHandler::handlePatch(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) { executeMethod("PATCH", req, res, url, params); }
void DynamicCrudHandler::handleDelete(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) { executeMethod("DELETE", req, res, url, params); }

void DynamicMetadataHandler::handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>& params) {
    if (!requirePermission(req, res, DynamicPermissionPolicy::permissionForDynamicRead())) return;
    DynamicQueryService service(config_);

    if (params.count("entity") && params.at("entity") == "tables") {
        sendApiResponse(res, service.metadataTables(getQueryParam(url, "q"), getQueryIntParam(url, "limit", INT_MAX, 1, INT_MAX), getQueryIntParam(url, "offset", 0, 0, INT_MAX)));
        return;
    }
    if (params.count("entity") && params.at("entity") == "allowlist") {
        sendApiResponse(res, service.metadataAllowlist());
        return;
    }
    if (params.count("table") && params.count("entity") && params.at("entity") == "fields") {
        sendApiResponse(res, service.metadataFields(params.at("table"), getQueryParam(url, "q")));
        return;
    }
    if (params.count("table") && params.count("entity") && params.at("entity") == "fields-info") {
        sendApiResponse(res, service.metadataFieldsInfo(params.at("table")));
        return;
    }
    if (params.count("entity") && params.at("entity") == "autocomplete") {
        const auto type = getQueryParam(url, "type");
        if (type == "tables") {
            sendApiResponse(res, service.metadataTables(getQueryParam(url, "q"), 50, 0));
        } else if (type == "fields") {
            sendApiResponse(res, service.metadataFields(getQueryParam(url, "table"), getQueryParam(url, "q")));
        } else {
            sendApiResponse(res, DynamicApiResponse::error(http::status::bad_request, "invalid_autocomplete_type", "type must be tables or fields"));
        }
        return;
    }
    sendApiResponse(res, DynamicApiResponse::error(http::status::not_found, "metadata_endpoint_not_found", "Metadata endpoint not found"));
}

void DynamicSchemaExportHandler::handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) {
    if (!requirePermission(req, res, DynamicPermissionPolicy::permissionForSchemaExport())) return;
    SchemaService service(config_);
    auto response = service.exportSchemaXml();
    if (response.status == http::status::ok && response.body.is_string()) {
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/xml; charset=utf-8");
        res.set(http::field::content_disposition, "inline; filename=\"dynamic.schema.xml\"");
        res.body() = response.body.as_string().c_str();
        res.prepare_payload();
        return;
    }
    sendApiResponse(res, response);
}

void DynamicSchemaValidateHandler::handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) {
    if (!requirePermission(req, res, DynamicPermissionPolicy::permissionForSchemaValidate())) return;
    SchemaService service(config_);
    sendApiResponse(res, service.validateXml(xmlFromRequestBody(req.body())));
}

void DynamicSchemaDiffHandler::handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) {
    if (!requirePermission(req, res, DynamicPermissionPolicy::permissionForSchemaDiff())) return;
    SchemaService service(config_);
    sendApiResponse(res, service.diffXml(xmlFromRequestBody(req.body())));
}

void DynamicSchemaPlanHandler::handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) {
    if (!requirePermission(req, res, DynamicPermissionPolicy::permissionForSchemaPlan())) return;
    SchemaPlanRequest request;
    request.xml = xmlFromRequestBody(req.body());
    SchemaService service(config_);
    sendApiResponse(res, service.planXml(request));
}

void DynamicSchemaApplyHandler::handlePost(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) {
    SchemaApplyRequest request;
    request.xml = xmlFromRequestBody(req.body());
    try {
        auto parsed = boost::json::parse(req.body());
        if (parsed.is_object()) {
            const auto& obj = parsed.as_object();
            if (auto it = obj.find("plan_id"); it != obj.end() && it->value().is_string()) request.planId = it->value().as_string().c_str();
            if (auto it = obj.find("destructive_confirmed"); it != obj.end() && it->value().is_bool()) request.destructiveConfirmed = it->value().as_bool();
            if (auto it = obj.find("manual_review_confirmed"); it != obj.end() && it->value().is_bool()) request.manualReviewConfirmed = it->value().as_bool();
            if (auto it = obj.find("dry_run"); it != obj.end() && it->value().is_bool()) request.dryRun = it->value().as_bool();
            if (auto it = obj.find("applied_by"); it != obj.end() && it->value().is_string()) request.appliedBy = it->value().as_string().c_str();
        }
    } catch (...) {
    }

    if (!requirePermission(req, res, DynamicPermissionPolicy::permissionForSchemaApply(request.destructiveConfirmed))) return;
    SchemaService service(config_);
    sendApiResponse(res, service.applyPlan(request));
}

void DynamicSchemaHistoryHandler::handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) {
    if (!requirePermission(req, res, DynamicPermissionPolicy::permissionForSchemaHistory())) return;
    SchemaService service(config_);
    sendApiResponse(res, service.history());
}

void DynamicOpenApiHandler::handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) {
    if (!requirePermission(req, res, "openapi.read")) return;
    DynamicOpenApiService service(config_);
    sendApiResponse(res, service.openApiJson());
}

void addSchemaDrivenDynamicApiRoutes(HttpServer& server, const DynamicApiConfig& config) {
    auto crud = std::make_shared<DynamicCrudHandler>(config);
    auto metadata = std::make_shared<DynamicMetadataHandler>(config);
    auto schemaExport = std::make_shared<DynamicSchemaExportHandler>(config);
    auto schemaValidate = std::make_shared<DynamicSchemaValidateHandler>(config);
    auto schemaDiff = std::make_shared<DynamicSchemaDiffHandler>(config);
    auto schemaPlan = std::make_shared<DynamicSchemaPlanHandler>(config);
    auto schemaApply = std::make_shared<DynamicSchemaApplyHandler>(config);
    auto schemaHistory = std::make_shared<DynamicSchemaHistoryHandler>(config);
    auto openapi = std::make_shared<DynamicOpenApiHandler>(config);

    server.add_route("/openapi.json", openapi);
    server.add_route(config.apiPrefix + "/openapi.json", openapi);

    server.add_route(config.schemaPrefix + "/export", schemaExport);
    server.add_route(config.schemaPrefix + "/validate", schemaValidate);
    server.add_route(config.schemaPrefix + "/diff", schemaDiff);
    server.add_route(config.schemaPrefix + "/plan", schemaPlan);
    server.add_route(config.schemaPrefix + "/apply", schemaApply);
    server.add_route(config.schemaPrefix + "/history", schemaHistory);

    // Backward-compatible XML export route from the original example.
    server.add_route(config.apiPrefix + "/schema.xml", schemaExport);

    server.add_route(config.apiPrefix + "/meta/{entity}", metadata);
    server.add_route(config.apiPrefix + "/meta/{table}/{entity}", metadata);

    server.add_route(config.apiPrefix, crud);
    server.add_route(config.apiPrefix + "/{table}", crud);
    server.add_route(config.apiPrefix + "/{table}/{id}", crud);
}

} // namespace qornix_dynamic_api
