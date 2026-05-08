/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <memory>
#include <utility>

#include "database_interface.h"
#include "table_manager.h"

// Declare the result structure
struct APIResult {
    std::string json_response;
    int status_code;

    // Default constructor
    APIResult() : json_response(""), status_code(200) {}

    // Constructor with parameters
    APIResult(std::string  response, int code)
        : json_response(std::move(response)), status_code(code) {}
};

class EntityAPIController {
private:
    std::shared_ptr<DatabaseInterface> db_;

    // Parse parameters with relations considered
    static std::string parseFilterCondition(const std::string& field, const std::string& value);

    // Convert the result to JSON
    static std::string convertToJSON(const std::vector<std::map<std::string, std::string>>& results, bool singleRecord = false);

    static std::map<std::string, std::string> parseJsonToMap(const std::string& jsonData);

    // Helper method for handling exceptions
    static APIResult handleException(const std::exception& e);

public:
    EntityAPIController(std::shared_ptr<DatabaseInterface> db);

    // Helper methods for creating JSON responses
    static std::string createErrorJSON(const std::string& message, const std::string& error_type = "error");

    // Methods compatible with the existing API (return only the JSON string)
    std::string handleGetRequest(const std::string& entityName, const std::map<std::string, std::string>& params);
    std::string handlePostRequest(const std::string& entityName, const std::string& jsonData);
    std::string handlePatchRequest(const std::string &entityName, const std::string &id, const std::string &jsonData);
    std::string handlePutRequest(const std::string& entityName, const std::string& id, const std::string& jsonData);
    std::string handleDeleteRequest(const std::string& entityName, const std::string& id);

    // New methods returning a status code for use in the web server
    APIResult handleGetRequestWithStatus(const std::string& entityName, const std::map<std::string, std::string>& params, bool singleRecord = false);
    APIResult handlePostRequestWithStatus(const std::string& entityName, const std::string& jsonData);
    APIResult handlePatchRequestWithStatus(const std::string& entityName, const std::string& id, const std::string& jsonData);
    APIResult handlePutRequestWithStatus(const std::string& entityName, const std::string& id, const std::string& jsonData);
    APIResult handleDeleteRequestWithStatus(const std::string& entityName, const std::string& id);
};
