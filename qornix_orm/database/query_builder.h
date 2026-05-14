/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#ifndef QUERY_BUILDER_H
#define QUERY_BUILDER_H

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <boost/json.hpp>
#include <iostream>

#ifndef QORNIX_ENABLE_ASYNC_DB
#define QORNIX_ENABLE_ASYNC_DB 0
#endif

#if QORNIX_ENABLE_ASYNC_DB
#include <boost/asio.hpp>
#include "db/async_db_types.h"
#endif

#include "database_interface.h"

// Enum for strict typing HTTP methods
enum class HttpMethod {
    GET,
    POST,
    PATCH,
    PUT,
    DELETE
};

// Function for working with HttpMethod enum
HttpMethod stringToHttpMethod(const std::string& method_str);
std::string httpMethodToString(HttpMethod method);

// Structure for input data
struct RequestData {
    HttpMethod method = HttpMethod::GET; // POST, PATCH, PUT, GET, DELETE
    std::string table;                // Name table
    std::vector<std::string> filters; // Conditions WHERE
    std::vector<std::string> groups;  // GROUP BY
    std::vector<std::string> orders;  // ORDER BY
    std::vector<std::string> fields;  // Fields SELECT
    std::vector<std::string> joins;   // JOIN
    std::vector<std::string> havings; // HAVING
    int limit_value = -1;             // LIMIT
    boost::json::object data;         // Data for POST/PATCH/PUT

    // Function for extraction values parameter query from URL
    static std::string extractUrlQuery(const std::string& url);

    // Main Function parsing
    void parseUrlQuery(const std::string& url_string);

    // Helper Function for splitting rows by character
    static std::vector<std::string> split(const std::string& str, char delimiter);

    // Parse from JSON
    void parseJsonQuery(const std::string& json_string);

    // Method for creation RequestData from table, method and rows request
    static RequestData buildRequestData(
        const std::string& query_string,
        const std::optional<std::string>& table = std::nullopt,
        const std::optional<std::string>& method = std::nullopt
    );


    // Convert in JSON
    std::string toJson() const;

    // Print raw data (for debugging)
    void printRawData() const;
};

// Structure for output data
struct ResponseData {
    boost::beast::http::status status = boost::beast::http::status::ok;  // HTTP status
    std::string message;             // Description result
    boost::json::array data;         // Result execution
    int count = 0;                   // Number of records

    // Convert in JSON
    std::string toJson() const;
};

// Main class QueryBuilder
class QueryBuilder {
private:
    std::vector<std::string> sql_parameters_;  // parameters for sql request
    bool has_sql_parameters_ = false; // Flag presence parameters for sql request
    std::function<std::string(std::size_t)> placeholder_formatter_;
    bool returning_supported_ = true;

    // Helper Method for handling filters with operators comparison
    std::string processFilterCondition(const std::string& condition);
    std::string placeholder(std::size_t one_based_index) const;

    ResponseData buildResponse(const std::vector<std::map<std::string, std::string>>& results);

    // Validation
    bool isValidIdentifier(const std::string& name);
    bool isValidFilter(const std::string& filter);
    bool isValidOrderBy(const std::string& order);
    bool isValidJoin(const std::string& join_clause);

public:
    // Default constructor
    QueryBuilder();

    RequestData request_data;
    std::shared_ptr<DatabaseInterface> db_interface;

    // Clear RequestData
    void clearRequestData();

    void addSqlParameter(const std::string& value);
    const std::vector<std::string>& getSqlParameters() const;
    bool hasSqlParameters() const; // Check presence parameters
    void setPlaceholderFormatter(std::function<std::string(std::size_t)> formatter);
    void setSupportsReturning(bool enabled);
    bool supportsReturning() const;

    // Initialize through configuration file
    static std::unique_ptr<QueryBuilder> init(const std::string& configPath = "config.yaml");

    // Constructor with support DatabaseInterface
    // QueryBuilder(std::shared_ptr<DatabaseInterface> db_interface = nullptr);

    // Set DatabaseInterface
    void setDatabaseInterface(std::shared_ptr<DatabaseInterface> db_interface);

    // Execute SQL-request without return result
    int executeNonQuery(const std::string& query);

    // Execute SQL-request and return result
    std::vector<std::map<std::string, std::string>> execute();

    // Set Method request
    QueryBuilder& setMethod(const std::string& method);

    // Set table
    QueryBuilder& setTable(const std::string& table);

    // Set data
    QueryBuilder& setData(const boost::json::object& data);

    // Methods for dynamic building request
    QueryBuilder& addFilter(const std::string& condition);
    QueryBuilder& addGroupBy(const std::string& field);
    QueryBuilder& addOrderBy(const std::string& field);
    QueryBuilder& addValue(const std::string& field);
    QueryBuilder& addJoin(const std::string& join_clause);
    QueryBuilder& addHaving(const std::string& condition);
    QueryBuilder& setLimit(int limit);

    // Parse JSON-request
    void parseRequest(
        const std::string& query_string,
        const std::optional<std::string>& table = std::nullopt,
        const std::optional<std::string>& method = std::nullopt
    );

    // Generate SQL-request
    std::string generateSQL();

    ResponseData getResponse();

    // Build JSON-response
    std::string getJsonResponse();

    // Get raw data (for debugging)
    void printRawData() const;

    ResponseData exec();
};


#if QORNIX_ENABLE_ASYNC_DB
class AsyncDatabaseInterface;

class AsyncQueryBuilder {
public:
    AsyncQueryBuilder();
    explicit AsyncQueryBuilder(std::shared_ptr<AsyncDatabaseInterface> database);

    void setDatabaseInterface(std::shared_ptr<AsyncDatabaseInterface> database);

    AsyncQueryBuilder& setMethod(const std::string& method);
    AsyncQueryBuilder& setTable(const std::string& table);
    AsyncQueryBuilder& setData(const boost::json::object& data);
    AsyncQueryBuilder& addFilter(const std::string& condition);
    AsyncQueryBuilder& addGroupBy(const std::string& field);
    AsyncQueryBuilder& addOrderBy(const std::string& field);
    AsyncQueryBuilder& addValue(const std::string& field);
    AsyncQueryBuilder& addJoin(const std::string& join_clause);
    AsyncQueryBuilder& addHaving(const std::string& condition);
    AsyncQueryBuilder& setLimit(int limit);
    AsyncQueryBuilder& queryOptions(qornix::db::QueryOptions options);
    AsyncQueryBuilder& timeout(std::chrono::milliseconds timeout);
    AsyncQueryBuilder& prepared(bool enabled = true);

    void parseRequest(
        const std::string& query_string,
        const std::optional<std::string>& table = std::nullopt,
        const std::optional<std::string>& method = std::nullopt
    );

    std::string generateSQL();
    const std::vector<std::string>& getSqlParameters() const;
    bool hasSqlParameters() const;

    boost::asio::awaitable<qornix::db::QueryResult> execute(qornix::db::CancellationToken token = {});
    boost::asio::awaitable<ResponseData> exec(qornix::db::CancellationToken token = {});
    boost::asio::awaitable<std::string> getJsonResponse(qornix::db::CancellationToken token = {});

private:
    qornix::db::QueryParams buildAsyncParams() const;
    ResponseData buildResponse(const qornix::db::QueryResult& result) const;
    void syncDialectOptions();

    QueryBuilder builder_;
    std::shared_ptr<AsyncDatabaseInterface> database_;
    qornix::db::QueryOptions query_options_{};
};
#endif

#endif // QUERY_BUILDER_H
