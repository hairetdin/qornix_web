/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <string>
#include <vector>
#include <map>
#include <boost/json.hpp>
#include <boost/beast/http/status.hpp>
#include "config.h"


// Structure for unified response database
struct DatabaseResponse {
    boost::beast::http::status http_status = boost::beast::http::status::ok;  // HTTP status
    std::string message;                          // Message about error or success
    std::vector<std::map<std::string, std::string>> data;  // Resulty request
    int affected_rows = 0;                        // Number of affected rows
    int count = 0;                                // Total number of records
    bool response_has_header = false;

    // Convert in JSON
    std::string toJson() const {
        boost::json::object json_response;
        json_response["status"] = std::to_string(static_cast<int>(http_status));
        json_response["message"] = message;
        json_response["affected_rows"] = affected_rows;
        json_response["count"] = count;

        boost::json::array data_array;
        for (const auto& row : data) {
            boost::json::object json_row;
            for (const auto& [key, value] : row) {
                json_row[key] = value;
            }
            data_array.push_back(json_row);
        }
        json_response["data"] = data_array;

        return boost::json::serialize(json_response);
    };
};

// Abstract interface for drivers databases data.
class IDatabase {
public:
    virtual ~IDatabase() = default;

    DatabaseConfig getDatabaseConfig() const;

    // Set connection with DB by connection string.
    virtual void connect(const std::string &connectionString) = 0;

    // Close connection.
    virtual void disconnect() = 0;

    // Unified Method execution requests with returning structured response
    virtual DatabaseResponse exec(const std::string& query, const std::vector<std::string>& params = {}) = 0;


    // Execute the request, returning data (for example, SELECT).
    virtual std::string executeQuery(const std::string &query, const std::string &dbSchema = "public") = 0;

    // Execute the request, not returning set data (for example, INSERT/UPDATE/DELETE).
    // Returned value - number of changed rows.
    virtual int executeNonQuery(const std::string &query) = 0;

    virtual std::string executeQueryWithHeaders(const std::string &query) = 0;
    virtual std::string executeQueryWithParams(const std::string& query, const std::vector<std::string>& params) = 0;


    // Flag connection status.
    virtual bool isConnected() const = 0;

    // Method for getting list tables
    virtual std::vector<std::string> getTableNames() = 0;

    // Method for getting names columns
    virtual std::vector<std::string> getColumnNames(const std::string &table_name) = 0;

    virtual int executeUpdate(const std::string &query) = 0;
};
