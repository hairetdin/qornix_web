/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include "dynamic_api_config.h"
#include "dynamic_api_response.h"
#include "dynamic_schema_allowlist.h"
#include "query_builder.h"

#include <boost/json.hpp>
#include <memory>
#include <optional>
#include <string>

namespace qornix_dynamic_api {

struct DynamicQueryRequest {
    std::string method;
    std::string table;
    std::string id;
    std::string rawBody;
    std::string rawQuery;
};

class DynamicQueryService {
public:
    explicit DynamicQueryService(DynamicApiConfig config);

    DynamicApiResponse execute(const DynamicQueryRequest& request) const;
    DynamicApiResponse metadataTables(const std::string& prefix, int limit, int offset) const;
    DynamicApiResponse metadataFields(const std::string& table, const std::string& prefix) const;
    DynamicApiResponse metadataFieldsInfo(const std::string& table) const;
    DynamicApiResponse metadataAllowlist() const;

private:
    DynamicApiConfig config_;

    std::shared_ptr<DatabaseInterface> openSharedDatabase() const;
    DynamicSchemaAllowlist loadAllowlist(DatabaseInterface& database) const;
    DynamicApiResponse validateRequest(const DynamicQueryRequest& request, const boost::json::object* body, const DynamicSchemaAllowlist& allowlist) const;
    DynamicApiResponse validateStructuredQuery(const std::string& table, const boost::json::object& query, const DynamicSchemaAllowlist& allowlist, QueryBuilder& builder) const;
    DynamicApiResponse applyBodyToBuilder(const DynamicQueryRequest& request, const boost::json::object* body, const DynamicSchemaAllowlist& allowlist, QueryBuilder& builder) const;

    static std::string jsonValueToSqlLiteral(const boost::json::value& value);
    static std::string normalizeOperator(const std::string& op);
    static std::string extractFieldNameFromRawCondition(const std::string& condition);
    static boost::json::object responseDataToJson(const ResponseData& responseData);
};

} // namespace qornix_dynamic_api
