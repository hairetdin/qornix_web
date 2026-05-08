/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <memory>
#include <boost/json.hpp>
#include <map>
#include <sstream>
#include <utility>

#include "entity_api_controller.h"

#include <iostream>

#include "handler_interface.h"
#include "../database/exceptions.h"

namespace {
std::string qualifyOrderByField(const std::string& ordering, const std::string& tableName) {
    if (ordering.empty()) {
        return ordering;
    }

    const bool descendingShortcut = ordering.front() == '-';
    std::string body = descendingShortcut ? ordering.substr(1) : ordering;
    const size_t spacePos = body.find(' ');
    std::string field = body.substr(0, spacePos);
    const std::string suffix = spacePos == std::string::npos ? "" : body.substr(spacePos);

    if (field.find('.') == std::string::npos && field.find('(') == std::string::npos) {
        field = tableName + "." + field;
    }

    return descendingShortcut ? "-" + field : field + suffix;
}
} // namespace


EntityAPIController::EntityAPIController(
    std::shared_ptr<DatabaseInterface> db)
    : db_(std::move(db)) {}

// Helper method for creating a JSON error response
std::string EntityAPIController::createErrorJSON(const std::string& message, const std::string& error_type) {
    return "{\"" + error_type + "\": \"" + message + "\"}";
}

// Helper method for handling exceptions and returning the corresponding response
APIResult EntityAPIController::handleException(const std::exception& e) {
    std::cout << "DEBUG: Exception caught: " << e.what() << std::endl;

    try {
        // Try casting to specific exception types
        const DatabaseError& dbError = dynamic_cast<const DatabaseError&>(e);

        // Check specific types
        try {
            const NotFoundError& notFound = dynamic_cast<const NotFoundError&>(e);
            std::cout << "DEBUG: Classified as NotFoundError" << std::endl;
            return APIResult(createErrorJSON(notFound.what()), 404);
        } catch (const std::bad_cast&) {
            // Not NotFoundError, continue checking
        }

        try {
            const ValidationError& validation = dynamic_cast<const ValidationError&>(e);
            std::cout << "DEBUG: Classified as ValidationError" << std::endl;
            return APIResult(createErrorJSON(validation.what()), 400);
        } catch (const std::bad_cast&) {
            // Not ValidationError, continue checking
        }

        try {
            const ConnectionError& connection = dynamic_cast<const ConnectionError&>(e);
            std::cout << "DEBUG: Classified as ConnectionError" << std::endl;
            return APIResult(createErrorJSON("Database connection error: " + std::string(connection.what())), 503);
        } catch (const std::bad_cast&) {
            // Not ConnectionError, continue checking
        }

        try {
            const ServerError& server = dynamic_cast<const ServerError&>(e);
            std::cout << "DEBUG: Classified as ServerError" << std::endl;
            return APIResult(createErrorJSON(server.what()), 500);
        } catch (const std::bad_cast&) {
            // Not ServerError
        }

        // If this is a DatabaseError, but not one of the types listed above
        std::cout << "DEBUG: Classified as generic DatabaseError" << std::endl;
        return APIResult(createErrorJSON(dbError.what()), 500);

    } catch (const std::bad_cast&) {
        // If this is not a DatabaseError at all
        std::cout << "DEBUG: Classified as generic exception" << std::endl;
        return APIResult(createErrorJSON(e.what()), 500);
    }
}

// Handle GET request
APIResult EntityAPIController::handleGetRequestWithStatus(
    const std::string& entityName,
    const std::map<std::string, std::string>& params,
    bool singleRecord) {

    try {
        auto tableManager = db_->table(entityName);

        // Apply filters
        for (const auto& param : params) {
            if (param.first == "order_by" || param.first == "limit" ||
                param.first == "offset" || param.first == "group_by" ||
                param.first == "having" || param.first == "values") {
                continue; // Handle special parameters below
            }
            // Apply filters with a table prefix to avoid conflicts
            tableManager.filter(entityName + "." + param.first + "=" + param.second);
        }

        // Apply special parameters
        if (params.count("values")) {
            std::string valuesStr = params.at("values");
            std::vector<std::string> fields;
            std::stringstream ss(valuesStr);
            std::string field;
            while (std::getline(ss, field, ',')) {
                field.erase(0, field.find_first_not_of(' '));
                field.erase(field.find_last_not_of(' ') + 1);
                fields.push_back(field);
            }
            tableManager.values(fields);
        }

        if (params.count("order_by")) {
            tableManager.order_by(qualifyOrderByField(params.at("order_by"), entityName));
        }

        if (params.count("group_by")) {
            tableManager.group_by(params.at("group_by"));
        }

        if (params.count("having")) {
            tableManager.having(params.at("having"));
        }

        if (singleRecord) {
            // Get a specific record (usually by ID from path parameters)
            auto result = tableManager.get(params);
            return APIResult(convertToJSON({result}, true), 200);
        } else {
            // Get the list of records
            if (params.count("limit")) {
                int limit = std::stoi(params.at("limit"));
                tableManager.limit(limit);
            }

            auto results = tableManager.execute();
            return APIResult(convertToJSON(results), 200);
        }

    } catch (const std::exception& e) {
        return handleException(e);
    }
}

// Handle GET request (compatibility with the existing API)
std::string EntityAPIController::handleGetRequest(
    const std::string& entityName,
    const std::map<std::string, std::string>& params) {

    APIResult result = handleGetRequestWithStatus(entityName, params);
    return result.json_response;
}

// Parse filter conditions
std::string EntityAPIController::parseFilterCondition(
    const std::string& field, const std::string& value) {

    size_t dotPos = field.find('.');
    if (dotPos != std::string::npos) {
        // This is a related field: category.name
        std::string entityName = field.substr(0, dotPos);
        std::string fieldName = field.substr(dotPos + 1);

        // Check for operators
        size_t opPos = fieldName.find("__");
        if (opPos != std::string::npos) {
            std::string actualField = fieldName.substr(0, opPos);
            std::string opType = fieldName.substr(opPos + 2);

            if (opType == "gte") {
                return entityName + "." + actualField + " >= " + value;
            } else if (opType == "gt") {
                return entityName + "." + actualField + " > " + value;
            } else if (opType == "lte") {
                return entityName + "." + actualField + " <= " + value;
            } else if (opType == "lt") {
                return entityName + "." + actualField + " < " + value;
            } else if (opType == "ne") {
                return entityName + "." + actualField + " != " + value;
            } else if (opType == "in") {
                return entityName + "." + actualField + " IN (" + value + ")";
            } else if (opType == "like") {
                return entityName + "." + actualField + " LIKE " + value;  // LIKE without quotes; TableManager will add them
            } else {
                return entityName + "." + actualField + " = " + value;
            }
        }

        return entityName + "." + fieldName + " = " + value;  // Without quotes; TableManager will add them itself
    }

    // Regular field
    // Check whether the value is numeric
    bool isNumeric = !value.empty() &&
                     std::all_of(value.begin(), value.end(), [](char c) {
                         return std::isdigit(c) || c == '.' || c == '-';
                     });

    // Additional check for floating-point numbers
    if (isNumeric) {
        size_t dotCount = std::count(value.begin(), value.end(), '.');
        isNumeric = (dotCount <= 1);
    }

    if (isNumeric) {
        return field + " = " + value;  // Numeric values without quotes
    } else {
        return field + " = " + value;  // String values without quotes; TableManager will add them itself
    }
}

// Handle POST request
APIResult EntityAPIController::handlePostRequestWithStatus(
    const std::string& entityName,
    const std::string& jsonData) {

    try {
        // Parse JSON data
        auto dataMap = parseJsonToMap(jsonData);

        auto result = db_->table(entityName).create(dataMap);
        return APIResult(convertToJSON({result}), 201);
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

// Handle POST request (compatibility with the existing API)
std::string EntityAPIController::handlePostRequest(
    const std::string& entityName,
    const std::string& jsonData) {

    APIResult result = handlePostRequestWithStatus(entityName, jsonData);
    return result.json_response;
}

APIResult EntityAPIController::handlePatchRequestWithStatus(
    const std::string& entityName,
    const std::string& id,
    const std::string& jsonData) {

    try {
        auto dataMap = parseJsonToMap(jsonData);

        // Check whether the record exists
        auto existingRecord = db_->table(entityName).get("id=" + id);

        if (existingRecord.empty()) {
            return APIResult(createErrorJSON("Record not found"), 404);
        }

        // Update only the fields present in the JSON data
        int updated = db_->table(entityName)
                     .filter("id=" + id)
                     .update(dataMap);

        if (updated > 0) {
            // Get the updated record
            auto updatedRecord = db_->table(entityName).get("id=" + id);
            return APIResult(convertToJSON({updatedRecord}), 200);
        } else {
            return APIResult(createErrorJSON("Failed to update record"), 500);
        }
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

std::string EntityAPIController::handlePatchRequest(
    const std::string& entityName,
    const std::string& id,
    const std::string& jsonData) {

    APIResult result = handlePatchRequestWithStatus(entityName, id, jsonData);
    return result.json_response;
}

// Handle PUT request
APIResult EntityAPIController::handlePutRequestWithStatus(
    const std::string& entityName,
    const std::string& id,
    const std::string& jsonData) {

    try {
        auto dataMap = parseJsonToMap(jsonData);

        int updated = db_->table(entityName)
                     .filter("id=" + id)
                     .update(dataMap);

        if (updated > 0) {
            auto updatedRecord = db_->table(entityName).get("id=" + id);
            return APIResult(convertToJSON({updatedRecord}), 200);
        } else {
            return APIResult(createErrorJSON("Record not found"), 404);
        }
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

// Handle PUT request (compatibility with the existing API)
std::string EntityAPIController::handlePutRequest(
    const std::string& entityName,
    const std::string& id,
    const std::string& jsonData) {

    APIResult result = handlePutRequestWithStatus(entityName, id, jsonData);
    return result.json_response;
}

// Handle DELETE request
APIResult EntityAPIController::handleDeleteRequestWithStatus(
    const std::string& entityName,
    const std::string& id) {

    try {
        int deleted = db_->table(entityName)
                     .filter("id=" + id)
                     .remove();

        if (deleted > 0) {
            return APIResult("{\"success\": true, \"deleted\": " + std::to_string(deleted) + "}", 200);
        } else {
            return APIResult(createErrorJSON("Record not found"), 404);
        }
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

// Handle DELETE request (compatibility with the existing API)
std::string EntityAPIController::handleDeleteRequest(
    const std::string& entityName,
    const std::string& id) {

    APIResult result = handleDeleteRequestWithStatus(entityName, id);
    return result.json_response;
}

// Convert to JSON
std::string EntityAPIController::convertToJSON(
    const std::vector<std::map<std::string, std::string>>& results,
    bool singleRecord) {

    // If the result is empty
    if (results.empty()) {
        return singleRecord ? "{}" : "[]";
    }

    // If one record needs to be returned
    if (singleRecord) {
        std::ostringstream json;
        json << "{";
        size_t fieldCount = 0;
        // Take the first record from the results
        for (const auto& field : results[0]) {
            if (fieldCount > 0) json << ",";
            json << "\"" << field.first << "\":\"" << field.second << "\"";
            fieldCount++;
        }
        json << "}";
        return json.str();
    }

    // Otherwise return an array of objects (default behavior)
    std::ostringstream json;
    json << "[";

    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) json << ",";

        json << "{";
        size_t fieldCount = 0;
        for (const auto& field : results[i]) {
            if (fieldCount > 0) json << ",";
            json << "\"" << field.first << "\":\"" << field.second << "\"";
            fieldCount++;
        }
        json << "}";
    }

    json << "]";
    return json.str();
}

std::map<std::string, std::string> EntityAPIController::parseJsonToMap(const std::string& jsonData) {
    std::map<std::string, std::string> result;

    try {
        // Parse JSON string
        boost::json::value jsonValue = boost::json::parse(jsonData);

        // Check if parsed value is an object
        if (jsonValue.is_object()) {
            boost::json::object jsonObject = jsonValue.get_object();

            // Iterate through all key-value pairs in the object
            for (const auto& member : jsonObject) {
                std::string key = member.key();
                std::string value;

                // Handle different JSON value types
                const boost::json::value& jsonVal = member.value();

                if (jsonVal.is_string()) {
                    value = jsonVal.get_string().c_str();
                } else if (jsonVal.is_int64()) {
                    value = std::to_string(jsonVal.get_int64());
                } else if (jsonVal.is_uint64()) {
                    value = std::to_string(jsonVal.get_uint64());
                } else if (jsonVal.is_double()) {
                    value = std::to_string(jsonVal.get_double());
                } else if (jsonVal.is_bool()) {
                    value = jsonVal.get_bool() ? "true" : "false";
                } else {
                    // For other types (array, object), convert to string representation
                    value = boost::json::serialize(jsonVal);
                }

                result[key] = value;
            }
        }
    } catch (const std::exception& e) {
        // Handle parsing errors
        throw ValidationError("JSON parsing error: " + std::string(e.what()));
    }

    return result;
}
