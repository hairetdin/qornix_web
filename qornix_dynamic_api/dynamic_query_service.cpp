/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "dynamic_query_service.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace qornix_dynamic_api {

namespace {
std::string getStringMember(const boost::json::object& obj, const std::string& key, const std::string& fallback = "") {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (it->value().is_string()) {
        return it->value().as_string().c_str();
    }
    return fallback;
}

int getIntMember(const boost::json::object& obj, const std::string& key, int fallback) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (it->value().is_int64()) {
        return static_cast<int>(it->value().as_int64());
    }
    if (it->value().is_uint64()) {
        return static_cast<int>(it->value().as_uint64());
    }
    return fallback;
}

bool startsWithCaseInsensitive(const std::string& value, const std::string& prefix) {
    if (prefix.empty()) return true;
    if (value.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}
}

DynamicQueryService::DynamicQueryService(DynamicApiConfig config)
    : config_(std::move(config)) {}

std::shared_ptr<DatabaseInterface> DynamicQueryService::openSharedDatabase() const {
    auto db = DatabaseInterface::init(config_.databaseConfig);
    return std::shared_ptr<DatabaseInterface>(db.release(), [](DatabaseInterface* ptr) { delete ptr; });
}

DynamicSchemaAllowlist DynamicQueryService::loadAllowlist(DatabaseInterface& database) const {
    return DynamicSchemaAllowlist::fromDatabase(database);
}

std::string DynamicQueryService::jsonValueToSqlLiteral(const boost::json::value& value) {
    if (value.is_null()) {
        return "NULL";
    }
    if (value.is_bool()) {
        return value.as_bool() ? "1" : "0";
    }
    if (value.is_int64()) {
        return std::to_string(value.as_int64());
    }
    if (value.is_uint64()) {
        return std::to_string(value.as_uint64());
    }
    if (value.is_double()) {
        std::ostringstream ss;
        ss << value.as_double();
        return ss.str();
    }
    std::string raw = value.is_string() ? std::string(value.as_string().c_str()) : boost::json::serialize(value);
    std::string escaped;
    escaped.reserve(raw.size() + 2);
    escaped.push_back('\'');
    for (char c : raw) {
        if (c == '\'') {
            escaped += "''";
        } else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

std::string DynamicQueryService::normalizeOperator(const std::string& op) {
    if (op == "eq" || op == "=") return "=";
    if (op == "ne" || op == "!=") return "!=";
    if (op == "gt" || op == ">") return ">";
    if (op == "gte" || op == ">=") return ">=";
    if (op == "lt" || op == "<") return "<";
    if (op == "lte" || op == "<=") return "<=";
    if (op == "like") return "LIKE";
    return "";
}

std::string DynamicQueryService::extractFieldNameFromRawCondition(const std::string& condition) {
    const std::string operators[] = {">=", "<=", "!=", "=", ">", "<", " LIKE ", " like "};
    std::size_t pos = std::string::npos;
    for (const auto& op : operators) {
        pos = condition.find(op);
        if (pos != std::string::npos) {
            break;
        }
    }
    std::string field = pos == std::string::npos ? condition : condition.substr(0, pos);
    field.erase(std::remove_if(field.begin(), field.end(), [](unsigned char c) { return std::isspace(c); }), field.end());
    auto dot = field.rfind('.');
    if (dot != std::string::npos) {
        field = field.substr(dot + 1);
    }
    return field;
}

boost::json::object DynamicQueryService::responseDataToJson(const ResponseData& responseData) {
    boost::json::object payload;
    payload["ok"] = static_cast<int>(responseData.status) >= 200 && static_cast<int>(responseData.status) < 400;
    payload["message"] = responseData.message;
    payload["count"] = responseData.count;
    payload["data"] = responseData.data;
    return payload;
}

DynamicApiResponse DynamicQueryService::validateRequest(const DynamicQueryRequest& request, const boost::json::object* body, const DynamicSchemaAllowlist& allowlist) const {
    const std::string table = request.table.empty() && body ? getStringMember(*body, "table") : request.table;
    if (table.empty()) {
        return DynamicApiResponse::error(boost::beast::http::status::bad_request, "table_required", "Dynamic API request requires a table");
    }
    if (!allowlist.hasTable(table)) {
        return DynamicApiResponse::error(boost::beast::http::status::bad_request, "table_not_allowed", "Table is not allowed by schema metadata: " + table);
    }

    if ((request.method == "GET" || request.method == "HEAD") && !allowlist.canReadTable(table)) {
        return DynamicApiResponse::error(boost::beast::http::status::forbidden, "table_read_forbidden", "Read operation is not allowed for table: " + table);
    }
    if ((request.method == "POST" || request.method == "PUT" || request.method == "PATCH") && !allowlist.canWriteTable(table)) {
        return DynamicApiResponse::error(boost::beast::http::status::forbidden, "table_write_forbidden", "Write operation is not allowed for table: " + table);
    }
    if (request.method == "DELETE" && !allowlist.canDeleteFromTable(table)) {
        return DynamicApiResponse::error(boost::beast::http::status::forbidden, "table_delete_forbidden", "Delete operation is not allowed for table: " + table);
    }

    if ((request.method == "PUT" || request.method == "PATCH" || request.method == "DELETE") && request.id.empty() && !config_.allowUpdateWithoutFilter) {
        if (request.method == "DELETE" && !config_.allowDeleteWithoutFilter) {
            return DynamicApiResponse::error(boost::beast::http::status::bad_request, "filter_required", "DELETE without id/filter is disabled by default");
        }
        if (request.method != "DELETE") {
            return DynamicApiResponse::error(boost::beast::http::status::bad_request, "filter_required", "UPDATE/PATCH without id/filter is disabled by default");
        }
    }

    return DynamicApiResponse::ok(okEnvelope("request allowed"));
}

DynamicApiResponse DynamicQueryService::validateStructuredQuery(const std::string& table, const boost::json::object& query, const DynamicSchemaAllowlist& allowlist, QueryBuilder& builder) const {
    auto fieldsIt = query.find("fields");
    if (fieldsIt != query.end() && fieldsIt->value().is_array()) {
        for (const auto& value : fieldsIt->value().as_array()) {
            if (!value.is_string()) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "invalid_field", "fields must contain strings");
            }
            std::string field = value.as_string().c_str();
            if (!allowlist.canReadField(table, field)) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "field_not_allowed", "Field is not readable by schema metadata: " + field);
            }
            builder.addValue(field);
        }
    }

    auto whereIt = query.find("where");
    if (whereIt != query.end() && whereIt->value().is_array()) {
        for (const auto& itemValue : whereIt->value().as_array()) {
            if (!itemValue.is_object()) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "invalid_where", "where items must be objects");
            }
            const auto& item = itemValue.as_object();
            std::string field = getStringMember(item, "field");
            auto dot = field.rfind('.');
            if (dot != std::string::npos) {
                field = field.substr(dot + 1);
            }
            std::string op = normalizeOperator(getStringMember(item, "op", "eq"));
            if (field.empty() || op.empty() || item.find("value") == item.end()) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "invalid_where", "where item requires field, op and value");
            }
            if (!allowlist.canFilterField(table, field)) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "field_not_filterable", "Field is not filterable by schema metadata: " + field);
            }
            builder.addFilter(field + op + jsonValueToSqlLiteral(item.at("value")));
        }
    }

    auto filterIt = query.find("filter");
    if (filterIt != query.end() && filterIt->value().is_array()) {
        for (const auto& value : filterIt->value().as_array()) {
            if (!value.is_string()) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "invalid_filter", "filter must contain strings");
            }
            std::string condition = value.as_string().c_str();
            auto field = extractFieldNameFromRawCondition(condition);
            if (field.empty() || !allowlist.canFilterField(table, field)) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "field_not_filterable", "Raw filter references a field not allowed by schema metadata: " + field);
            }
            builder.addFilter(condition);
        }
    }

    auto orderIt = query.find("order_by");
    if (orderIt != query.end() && orderIt->value().is_array()) {
        for (const auto& value : orderIt->value().as_array()) {
            if (!value.is_string()) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "invalid_order", "order_by must contain strings");
            }
            std::string order = value.as_string().c_str();
            std::string field = order;
            auto space = field.find(' ');
            if (space != std::string::npos) field = field.substr(0, space);
            auto dot = field.rfind('.');
            if (dot != std::string::npos) field = field.substr(dot + 1);
            if (!allowlist.canSortField(table, field)) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "field_not_sortable", "Field is not sortable by schema metadata: " + field);
            }
            builder.addOrderBy(order);
        }
    }

    auto joinIt = query.find("join");
    if (joinIt != query.end() && joinIt->value().is_array()) {
        if (!config_.allowRawJoin) {
            return DynamicApiResponse::error(boost::beast::http::status::bad_request, "raw_join_disabled", "Raw join DSL is disabled by default. Use schema-defined relations in future API versions.");
        }
        for (const auto& value : joinIt->value().as_array()) {
            if (value.is_string()) builder.addJoin(value.as_string().c_str());
        }
    }

    auto groupsIt = query.find("group_by");
    if (groupsIt != query.end() && groupsIt->value().is_array()) {
        for (const auto& value : groupsIt->value().as_array()) {
            if (value.is_string()) builder.addGroupBy(value.as_string().c_str());
        }
    }

    auto havingIt = query.find("having");
    if (havingIt != query.end() && havingIt->value().is_array()) {
        for (const auto& value : havingIt->value().as_array()) {
            if (value.is_string()) builder.addHaving(value.as_string().c_str());
        }
    }

    const int rawLimit = getIntMember(query, "limit", static_cast<int>(config_.defaultLimit));
    const int limit = std::max(1, std::min(rawLimit, static_cast<int>(config_.maxLimit)));
    builder.setLimit(limit);

    return DynamicApiResponse::ok(okEnvelope("query allowed"));
}

DynamicApiResponse DynamicQueryService::applyBodyToBuilder(const DynamicQueryRequest& request, const boost::json::object* body, const DynamicSchemaAllowlist& allowlist, QueryBuilder& builder) const {
    std::string table = request.table;
    std::string method = request.method;
    if (body) {
        table = table.empty() ? getStringMember(*body, "table") : table;
        method = getStringMember(*body, "method", method);
    }
    builder.setTable(table);
    builder.setMethod(method);

    if (!request.id.empty()) {
        builder.addFilter("id=" + request.id);
    }

    if (body) {
        auto queryIt = body->find("query");
        if (queryIt != body->end() && queryIt->value().is_object()) {
            auto queryResponse = validateStructuredQuery(table, queryIt->value().as_object(), allowlist, builder);
            if (queryResponse.status != boost::beast::http::status::ok) {
                return queryResponse;
            }
        } else {
            builder.setLimit(static_cast<int>(config_.defaultLimit));
        }

        auto dataIt = body->find("data");
        if (dataIt != body->end() && dataIt->value().is_object()) {
            boost::json::object filteredData;
            for (const auto& [key, value] : dataIt->value().as_object()) {
                std::string fieldName(key);
                if (!allowlist.canWriteField(table, fieldName)) {
                    return DynamicApiResponse::error(boost::beast::http::status::bad_request, "field_not_writable", "Field is not writable by schema metadata: " + fieldName);
                }
                filteredData[fieldName] = value;
            }
            builder.setData(filteredData);
        }
    } else if (!request.rawQuery.empty()) {
        if (!config_.allowRawFilter) {
            return DynamicApiResponse::error(boost::beast::http::status::bad_request, "raw_query_disabled", "Raw URL query DSL is disabled by default");
        }
        builder.parseRequest(request.rawQuery, table, method);
    } else {
        builder.setLimit(static_cast<int>(config_.defaultLimit));
    }

    return DynamicApiResponse::ok(okEnvelope("builder prepared"));
}

DynamicApiResponse DynamicQueryService::execute(const DynamicQueryRequest& request) const {
    try {
        auto database = openSharedDatabase();
        auto allowlist = loadAllowlist(*database);

        boost::json::object parsedBody;
        boost::json::object* body = nullptr;
        if (!request.rawBody.empty()) {
            auto parsed = boost::json::parse(request.rawBody);
            if (!parsed.is_object()) {
                return DynamicApiResponse::error(boost::beast::http::status::bad_request, "invalid_json", "Dynamic API request body must be a JSON object");
            }
            parsedBody = parsed.as_object();
            body = &parsedBody;
        }

        auto validation = validateRequest(request, body, allowlist);
        if (validation.status != boost::beast::http::status::ok) {
            return validation;
        }

        QueryBuilder builder;
        builder.setDatabaseInterface(database);
        auto prepared = applyBodyToBuilder(request, body, allowlist, builder);
        if (prepared.status != boost::beast::http::status::ok) {
            return prepared;
        }

        auto result = builder.getResponse();
        DynamicApiResponse response;
        response.status = result.status;
        response.message = result.message;
        response.body = responseDataToJson(result);
        return response;
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "dynamic_query_failed", e.what());
    }
}

DynamicApiResponse DynamicQueryService::metadataTables(const std::string& prefix, int limit, int offset) const {
    try {
        auto database = openSharedDatabase();
        auto allowlist = loadAllowlist(*database);
        boost::json::array items;
        int skipped = 0;
        for (const auto& table : allowlist.tables()) {
            if (!startsWithCaseInsensitive(table, prefix)) continue;
            if (skipped < offset) { ++skipped; continue; }
            if (limit > 0 && static_cast<int>(items.size()) >= limit) break;
            items.emplace_back(table);
        }
        boost::json::object payload = okEnvelope("Tables loaded");
        payload["items"] = items;
        payload["data"] = items;
        payload["count"] = static_cast<std::uint64_t>(items.size());
        return DynamicApiResponse::ok(payload, "Tables loaded");
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "metadata_tables_failed", e.what());
    }
}

DynamicApiResponse DynamicQueryService::metadataFields(const std::string& table, const std::string& prefix) const {
    try {
        auto database = openSharedDatabase();
        auto allowlist = loadAllowlist(*database);
        if (!allowlist.hasTable(table)) {
            return DynamicApiResponse::error(boost::beast::http::status::bad_request, "table_not_allowed", "Unknown table: " + table);
        }
        boost::json::array items;
        for (const auto& field : allowlist.fieldsForTable(table)) {
            if (startsWithCaseInsensitive(field, prefix)) {
                items.emplace_back(field);
            }
        }
        boost::json::object payload = okEnvelope("Fields loaded");
        payload["items"] = items;
        payload["data"] = items;
        payload["count"] = static_cast<std::uint64_t>(items.size());
        return DynamicApiResponse::ok(payload, "Fields loaded");
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "metadata_fields_failed", e.what());
    }
}

DynamicApiResponse DynamicQueryService::metadataFieldsInfo(const std::string& table) const {
    try {
        auto database = openSharedDatabase();
        auto allowlist = loadAllowlist(*database);
        if (!allowlist.hasTable(table)) {
            return DynamicApiResponse::error(boost::beast::http::status::bad_request, "table_not_allowed", "Unknown table: " + table);
        }
        boost::json::array items;
        for (const auto& field : allowlist.fieldsForTable(table)) {
            boost::json::object item;
            item["name"] = field;
            item["verboseName"] = field;
            items.emplace_back(item);
        }
        boost::json::object payload = okEnvelope("Fields info loaded");
        payload["items"] = items;
        payload["data"] = items;
        payload["count"] = static_cast<std::uint64_t>(items.size());
        return DynamicApiResponse::ok(payload, "Fields info loaded");
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "metadata_fields_info_failed", e.what());
    }
}

DynamicApiResponse DynamicQueryService::metadataAllowlist() const {
    try {
        auto database = openSharedDatabase();
        auto allowlist = loadAllowlist(*database);
        boost::json::object payload = okEnvelope("Allowlist loaded");
        payload["allowlist"] = allowlist.toJson();
        return DynamicApiResponse::ok(payload, "Allowlist loaded");
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "allowlist_failed", e.what());
    }
}

} // namespace qornix_dynamic_api
