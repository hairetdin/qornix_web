/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "query_builder.h"
#if QORNIX_ENABLE_ASYNC_DB
#include "async_database_interface.h"
#endif
#include <boost/json.hpp>
#include <stdexcept>
#include <sstream>
#include <utility>
#include <cctype>

namespace {
std::string trimCopy(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string jsonValueToSqlParameter(const boost::json::value& value) {
    if (value.is_string()) return std::string(value.as_string().c_str());
    if (value.is_int64()) return std::to_string(value.as_int64());
    if (value.is_uint64()) return std::to_string(value.as_uint64());
    if (value.is_double()) return std::to_string(value.as_double());
    if (value.is_bool()) return value.as_bool() ? "true" : "false";
    if (value.is_null()) return "NULL";
    return boost::json::serialize(value);
}
} // namespace


RequestData RequestData::buildRequestData(
    const std::string &query_string,
    const std::optional<std::string> &request_table,
    const std::optional<std::string> &request_method) {
    RequestData requestData;
    if (request_table.has_value()) {
        requestData.table = request_table.value();
    }
    if (request_method.has_value()) {
        requestData.method = stringToHttpMethod(request_method.value());
    }

    // Determine Type request by content
    if (!query_string.empty()) {
        // If string starts with '{', treat as it JSON
        if (query_string.front() == '{') {
            requestData.parseJsonQuery(query_string);
        } else {
            // Otherwise Parse as URL string
            requestData.parseUrlQuery(query_string);
        }
    }

    return requestData;
}

// Implementation functions for working with HttpMethod enum
HttpMethod stringToHttpMethod(const std::string &method_str) {
    std::string upper_method = method_str;
    std::transform(upper_method.begin(), upper_method.end(), upper_method.begin(), ::toupper);

    if (upper_method == "GET") return HttpMethod::GET;
    if (upper_method == "POST") return HttpMethod::POST;
    if (upper_method == "PATCH") return HttpMethod::PATCH;
    if (upper_method == "PUT") return HttpMethod::PUT;
    if (upper_method == "DELETE") return HttpMethod::DELETE;

    throw std::invalid_argument("Unknown HTTP method: " + method_str);
}

std::string httpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PATCH: return "PATCH";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        default: throw std::invalid_argument("Unknown HttpMethod value");
    }
}

// Implementation RequestData
// Function for extraction values parameter query from URL
std::string RequestData::extractUrlQuery(const std::string &url) {
    size_t queryPos = url.find("query=");
    if (queryPos == std::string::npos) {
        throw std::invalid_argument("Parameter 'query' not found in URL");
    }

    // Extract part after query=
    std::string queryPart = url.substr(queryPos + 6); // +6 for "query="

    // If is additional parameters after query, truncate ikh
    size_t ampersandPos = queryPart.find('&');
    if (ampersandPos != std::string::npos) {
        queryPart = queryPart.substr(0, ampersandPos);
    }

    return queryPart;
}

// Main Function parsing
void RequestData::parseUrlQuery(const std::string &url_string) {
    std::string queryStr;

    // Determine, is whether input full URL or already content query
    if (url_string.find("query=") != std::string::npos) {
        queryStr = extractUrlQuery(url_string);
    } else {
        queryStr = url_string;
    }

    // Remove foreign brackets
    if (!queryStr.empty() && queryStr.front() == '(' && queryStr.back() == ')') {
        queryStr = queryStr.substr(1, queryStr.size() - 2);
    }

    // Split by dot with comma
    std::stringstream ss(queryStr);
    std::string pair;
    while (std::getline(ss, pair, ';')) {
        size_t colonPos = pair.find(':');
        if (colonPos == std::string::npos) continue;

        std::string key = pair.substr(0, colonPos);
        std::string valueBlock = pair.substr(colonPos + 1);

        // Remove brackets from block values
        if (!valueBlock.empty() && valueBlock.front() == '[' && valueBlock.back() == ']') {
            valueBlock = valueBlock.substr(1, valueBlock.size() - 2);
        }

        // Fill structure
        if (key == "table") {
            table = valueBlock;
        } else if (key == "method") {
            method = stringToHttpMethod(valueBlock);
        } else if (key == "fields") {
            fields = split(valueBlock, ',');
        } else if (key == "filters") {
            filters = split(valueBlock, ',');
        } else if (key == "groups") {
            groups = split(valueBlock, ',');
        } else if (key == "orders") {
            orders = split(valueBlock, ',');
        } else if (key == "joins") {
            joins = split(valueBlock, ',');
        } else if (key == "havings") {
            havings = split(valueBlock, ',');
        } else if (key == "limit") {
            limit_value = std::stoi(valueBlock);
        } else if (key == "data") {
            // Parse data in format key=value,key2=value2
            std::vector<std::string> keyValuePairs = split(valueBlock, ',');
            for (const auto &data_pair: keyValuePairs) {
                size_t equalPos = data_pair.find('=');
                if (equalPos != std::string::npos) {
                    std::string dataKey = data_pair.substr(0, equalPos);
                    std::string dataValue = data_pair.substr(equalPos + 1);
                    // Remove possible spaces
                    dataKey.erase(0, dataKey.find_first_not_of(' '));
                    dataKey.erase(dataKey.find_last_not_of(' ') + 1);
                    dataValue.erase(0, dataValue.find_first_not_of(' '));
                    dataValue.erase(dataValue.find_last_not_of(' ') + 1);

                    data[dataKey] = dataValue;
                }
            }
        }
    }
    // printRawData();
}

// Helper Function for splitting rows by character
std::vector<std::string> RequestData::split(const std::string &str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

void RequestData::parseJsonQuery(const std::string &json_string) {
    auto json_request = boost::json::parse(json_string).as_object();

    // Check is whether JSON structured request or flat data
    bool is_structured_request = json_request.contains("method") ||
                                 json_request.contains("table") ||
                                 json_request.contains("query") ||
                                 json_request.contains("data");

    if (is_structured_request) {
        if (json_request.contains("method")) {
            method = stringToHttpMethod(json_request["method"].as_string().c_str());
        }
        if (json_request.contains("table")) {
            table = json_request["table"].as_string().c_str();
        }

        if (json_request.contains("query")) {
            auto query_obj = json_request["query"].as_object();

            // Parse fields (SELECT fields)
            if (query_obj.contains("fields")) {
                for (const auto &item: query_obj["fields"].as_array()) {
                    fields.push_back(item.as_string().c_str());
                }
            }

            // Parse filters
            if (query_obj.contains("filter")) {
                for (const auto &item: query_obj["filter"].as_array()) {
                    filters.push_back(item.as_string().c_str());
                }
            }
            if (query_obj.contains("where")) {
                for (const auto &item: query_obj["where"].as_array()) {
                    filters.push_back(item.as_string().c_str());
                }
            }

            // Parse group_by
            if (query_obj.contains("group_by")) {
                for (const auto &item: query_obj["group_by"].as_array()) {
                    groups.push_back(item.as_string().c_str());
                }
            }

            // Parse order_by
            if (query_obj.contains("order_by")) {
                for (const auto &item: query_obj["order_by"].as_array()) {
                    orders.push_back(item.as_string().c_str());
                }
            }

            // Parse joins
            if (query_obj.contains("join")) {
                for (const auto &item: query_obj["join"].as_array()) {
                    joins.push_back(item.as_string().c_str());
                }
            }

            // Parse limit
            if (query_obj.contains("limit")) {
                limit_value = query_obj["limit"].as_int64();
            }
        }

        if (json_request.contains("data")) {
            data = json_request["data"].as_object();
        }
    } else {
        // Flat JSON - this data for PATCH/PUT (for example {"city":"Denver1"})
        data = json_request;
    }
}


std::string RequestData::toJson() const {
    boost::json::object json_request;
    json_request["method"] = httpMethodToString(method);
    json_request["table"] = table;

    boost::json::object query_obj;
    if (!filters.empty()) {
        boost::json::array filter_arr;
        for (const auto &filter: filters) {
            filter_arr.push_back(boost::json::value(filter));
        }
        query_obj["filter"] = filter_arr;
    }

    if (!groups.empty()) {
        boost::json::array group_arr;
        for (const auto &group: groups) {
            group_arr.push_back(boost::json::value(group));
        }
        query_obj["group_by"] = group_arr;
    }

    if (!orders.empty()) {
        boost::json::array order_arr;
        for (const auto &order: orders) {
            order_arr.push_back(boost::json::value(order));
        }
        query_obj["order_by"] = order_arr;
    }

    if (limit_value >= 0) {
        query_obj["limit"] = limit_value;
    }

    if (!query_obj.empty()) {
        json_request["query"] = query_obj;
    }

    if (!data.empty()) {
        json_request["data"] = data;
    }

    return boost::json::serialize(json_request);
}


void RequestData::printRawData() const {
    std::cout << "=== Request Data ===" << "\n";
    std::cout << "Method: " << httpMethodToString(method) << "\n";
    std::cout << "Table: " << table << "\n";

    // Output fields SELECT
    if (!fields.empty()) {
        std::cout << "Fields: ";
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << fields[i];
        }
        std::cout << "\n";
    }

    // Output filters WHERE
    if (!filters.empty()) {
        std::cout << "Filters: ";
        for (size_t i = 0; i < filters.size(); ++i) {
            if (i > 0) std::cout << " AND ";
            std::cout << filters[i];
        }
        std::cout << "\n";
    }

    // Output grouping GROUP BY
    if (!groups.empty()) {
        std::cout << "Groups: ";
        for (size_t i = 0; i < groups.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << groups[i];
        }
        std::cout << "\n";
    }

    // Output sorting ORDER BY
    if (!orders.empty()) {
        std::cout << "Orders: ";
        for (size_t i = 0; i < orders.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << orders[i];
        }
        std::cout << "\n";
    }

    // Output JOIN's
    if (!joins.empty()) {
        std::cout << "Joins: ";
        for (size_t i = 0; i < joins.size(); ++i) {
            if (i > 0) std::cout << "; ";
            std::cout << joins[i];
        }
        std::cout << "\n";
    }

    // Output conditions HAVING
    if (!havings.empty()) {
        std::cout << "Havings: ";
        for (size_t i = 0; i < havings.size(); ++i) {
            if (i > 0) std::cout << " AND ";
            std::cout << havings[i];
        }
        std::cout << "\n";
    }

    // Output limit
    if (limit_value >= 0) {
        std::cout << "Limit: " << limit_value << "\n";
    }

    // Output data for POST/PATCH/PUT requests
    if (!data.empty() && (method == HttpMethod::POST || method == HttpMethod::PATCH || method == HttpMethod::PUT)) {
        std::cout << "Data: " << "\n";
        for (const auto &[key, value]: data) {
            std::cout << "  " << key << ": " << value << "\n";
        }
    }

    std::cout << "===================" << "\n";
}

// Implementation ResponseData
std::string ResponseData::toJson() const {
    boost::json::object json_response;
    json_response["status"] = std::to_string(static_cast<int>(status));
    json_response["message"] = message;
    json_response["data"] = data;
    json_response["count"] = count;
    return boost::json::serialize(json_response);
}

// Default constructor
QueryBuilder::QueryBuilder() : db_interface(nullptr) {
}

void QueryBuilder::clearRequestData() {
    request_data = RequestData(); // Reset all parameters
}

void QueryBuilder::addSqlParameter(const std::string &value) {
    sql_parameters_.push_back(value);
    has_sql_parameters_ = true; // Set Flag
}

const std::vector<std::string> &QueryBuilder::getSqlParameters() const {
    return sql_parameters_;
}

bool QueryBuilder::hasSqlParameters() const {
    return has_sql_parameters_;
}

void QueryBuilder::setPlaceholderFormatter(std::function<std::string(std::size_t)> formatter) {
    placeholder_formatter_ = std::move(formatter);
}

void QueryBuilder::setSupportsReturning(bool enabled) {
    returning_supported_ = enabled;
}

bool QueryBuilder::supportsReturning() const {
    return returning_supported_;
}

std::string QueryBuilder::placeholder(std::size_t one_based_index) const {
    if (placeholder_formatter_) {
        return placeholder_formatter_(one_based_index);
    }
    return "$" + std::to_string(one_based_index);
}

// validation
bool QueryBuilder::isValidIdentifier(const std::string &name) {
    if (name.empty()) return false;

    // Allow letters, digits, underscore and dot
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(c) || c == '_' || c == '.';
    });
}

bool QueryBuilder::isValidFilter(const std::string &filter) {
    // Supported operators comparison
    std::vector<std::string> operators = {"=", ">", "<", ">=", "<=", "!="};

    // Look for any from operators
    size_t op_pos = std::string::npos;
    std::string found_operator;

    for (const auto &op: operators) {
        size_t pos = filter.find(op);
        if (pos != std::string::npos) {
            if (op_pos == std::string::npos || pos < op_pos) {
                op_pos = pos;
                found_operator = op;
            }
        }
    }

    if (op_pos == std::string::npos) return false;

    std::string column = filter.substr(0, op_pos);
    column.erase(std::remove_if(column.begin(), column.end(), ::isspace), column.end());

    // Check that Name fields correctly
    if (!isValidIdentifier(column)) {
        return false;
    }

    // Base blocklist for obvious injections (main protection - parameterization)
    std::string value_part = filter.substr(op_pos + found_operator.length());
    std::string upper = value_part;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (value_part.find(';') != std::string::npos ||
        upper.find("--") != std::string::npos ||
        upper.find("/*") != std::string::npos ||
        upper.find("*/") != std::string::npos ||
        upper.find(" OR ") != std::string::npos ||
        upper.find(" AND ") != std::string::npos) {
        return false;
    }

    return true;
}

bool QueryBuilder::isValidOrderBy(const std::string &order) {
    // Example: "departments.name ASC"
    size_t space_pos = order.find(' ');
    if (space_pos == std::string::npos) return false;

    std::string column = order.substr(0, space_pos);
    std::string direction = order.substr(space_pos + 1);

    // Check that Name fields correctly (now with dot)
    return isValidIdentifier(column) &&
           (direction == "ASC" || direction == "DESC");
}

bool QueryBuilder::isValidJoin(const std::string &join_clause) {
    // Example: "JOIN departments ON users.department_id = departments.id"
    std::istringstream stream(join_clause);
    std::string token;
    std::vector<std::string> tokens;

    while (stream >> token) {
        tokens.push_back(token);
    }

    // Minimum number of parts: 4
    if (tokens.size() < 4) {
        return false;
    }

    size_t table_index = 1;
    size_t on_index = 2;
    std::string join_type = tokens[0];
    if (join_type == "JOIN") {
        table_index = 1;
        on_index = 2;
    } else if (join_type == "INNER" || join_type == "LEFT" || join_type == "RIGHT" || join_type == "FULL") {
        if (tokens.size() >= 6 && tokens[1] == "OUTER" && tokens[2] == "JOIN") {
            table_index = 3;
            on_index = 4;
        } else if (tokens.size() >= 5 && tokens[1] == "JOIN") {
            table_index = 2;
            on_index = 3;
        } else {
            return false;
        }
    } else {
        return false;
    }

    // Check Name table
    std::string table_name = tokens[table_index];
    if (!isValidIdentifier(table_name)) {
        return false;
    }

    // Check keyword word ON
    if (tokens[on_index] != "ON") {
        return false;
    }

    // Check condition (must contain '=' and valid names fields)
    std::string condition = join_clause.substr(join_clause.find("ON") + 3);
    size_t eq_pos = condition.find('=');
    if (eq_pos == std::string::npos) {
        return false;
    }

    std::string left_side = condition.substr(0, eq_pos);
    std::string right_side = condition.substr(eq_pos + 1);

    left_side.erase(std::remove_if(left_side.begin(), left_side.end(), ::isspace), left_side.end());
    right_side.erase(std::remove_if(right_side.begin(), right_side.end(), ::isspace), right_side.end());

    return isValidIdentifier(left_side) && isValidIdentifier(right_side);
}

// Initialize through configuration file
std::unique_ptr<QueryBuilder> QueryBuilder::init(const std::string &configPath) {
    try {
        // Initialize DatabaseInterface
        std::shared_ptr<DatabaseInterface> db_interface = DatabaseInterface::init(configPath);

        // Create QueryBuilder and Set DatabaseInterface
        auto builder = std::make_unique<QueryBuilder>();
        builder->setDatabaseInterface(db_interface);

        return builder;
    } catch (const std::exception &e) {
        throw std::runtime_error("Failed to initialize QueryBuilder: " + std::string(e.what()));
    }
}

// Constructor QueryBuilder
// QueryBuilder::QueryBuilder(std::shared_ptr<DatabaseInterface> db_interface)
//     : db_interface(std::move(db_interface)) {}

// Set DatabaseInterface
void QueryBuilder::setDatabaseInterface(std::shared_ptr<DatabaseInterface> db_interface_) {
    db_interface = std::move(db_interface_);
}

int QueryBuilder::executeNonQuery(const std::string &query) {
    if (!db_interface || !db_interface->isConnected()) {
        throw std::runtime_error("Database interface is not connected");
    }
    return db_interface->executeNonQuery(query);
}

// Execute SQL-request
std::vector<std::map<std::string, std::string> > QueryBuilder::execute() {
    if (!db_interface || !db_interface->isConnected()) {
        throw std::runtime_error("Database interface is not connected");
    }

    std::string sql = generateSQL();
    // std::string raw_result = db_interface->executeQueryWithHeaders(sql);
    std::string raw_result;

    if (hasSqlParameters()) {
        // Execute parameterized request
        std::vector<std::string> params_copy = getSqlParameters();
        raw_result = db_interface->executeQueryWithParams(sql, params_copy);
    } else {
        // Execute regular request
        raw_result = db_interface->executeQueryWithHeaders(sql);
    }

    // Parse result
    std::vector<std::map<std::string, std::string> > parsed_result;
    std::istringstream result_stream(raw_result);
    std::string line;

    // Reading headers (first string)
    if (std::getline(result_stream, line)) {
        std::vector<std::string> headers;
        std::istringstream header_stream(line);
        std::string header;
        while (std::getline(header_stream, header, '\t')) {
            headers.push_back(header);
        }

        // Reading data
        while (std::getline(result_stream, line)) {
            std::map<std::string, std::string> row;
            std::istringstream row_stream(line);
            std::string value;
            size_t col_index = 0;

            while (std::getline(row_stream, value, '\t') && col_index < headers.size()) {
                row[headers[col_index]] = value;
                ++col_index;
            }

            parsed_result.push_back(row);
        }
    }

    return parsed_result;
}

// Implementation QueryBuilder
QueryBuilder &QueryBuilder::setMethod(const std::string &method) {
    request_data.method = stringToHttpMethod(method);
    return *this;
}

QueryBuilder &QueryBuilder::setTable(const std::string &table) {
    request_data.table = table;
    return *this;
}

QueryBuilder &QueryBuilder::setData(const boost::json::object &data) {
    request_data.data = data;
    return *this;
}

QueryBuilder &QueryBuilder::addFilter(const std::string &condition) {
    request_data.filters.push_back(condition);
    return *this;
}

QueryBuilder &QueryBuilder::addGroupBy(const std::string &field) {
    request_data.groups.push_back(field);
    return *this;
}

QueryBuilder &QueryBuilder::addOrderBy(const std::string &field) {
    request_data.orders.push_back(field);
    return *this;
}

QueryBuilder &QueryBuilder::addValue(const std::string &field) {
    request_data.fields.push_back(field);
    return *this;
}

QueryBuilder &QueryBuilder::addJoin(const std::string &join_clause) {
    request_data.joins.push_back(join_clause);
    return *this;
}

QueryBuilder &QueryBuilder::addHaving(const std::string &condition) {
    request_data.havings.push_back(condition);
    return *this;
}

QueryBuilder &QueryBuilder::setLimit(int limit) {
    request_data.limit_value = limit;
    return *this;
}

void QueryBuilder::parseRequest(
    const std::string &query_string, // string request from url or json body
    const std::optional<std::string> &table,
    const std::optional<std::string> &method
) {
    // std::cout << "DEBUG: Parsing request..." << std::endl;
    // std::cout << "DEBUG: Query string: " << query_string << std::endl;
    // std::cout << "DEBUG: Table: " << table.value_or("") << std::endl;
    // std::cout << "DEBUG: Method: " << method.value_or("") << std::endl;

    request_data = RequestData::buildRequestData(query_string, table, method);

    // std::cout << request_data.toJson() << std::endl;
    // request_data.printRawData();
    // request_data.parseJsonQuery(json_string);
}

// Helper Method for handling filters with operators comparison
std::string QueryBuilder::processFilterCondition(const std::string &condition) {
    // Supported operators comparison
    std::vector<std::string> operators = {">=", "<=", "!=", "=", ">", "<"};
    size_t op_pos = std::string::npos;
    std::string found_operator;

    // Look for any from operators
    for (const auto &op: operators) {
        size_t pos = condition.find(op);
        if (pos != std::string::npos) {
            if (op_pos == std::string::npos || pos < op_pos) {
                op_pos = pos;
                found_operator = op;
            }
        }
    }

    if (op_pos != std::string::npos) {
        std::string column = trimCopy(condition.substr(0, op_pos));
        std::string value_part = trimCopy(condition.substr(op_pos + found_operator.length()));

        if (column.empty()) {
            throw std::runtime_error("Invalid filter: empty column in condition: " + condition);
        }
        if (value_part.empty()) {
            throw std::runtime_error("Invalid filter: empty value in condition: " + condition);
        }

        // Remove foreign quotes, if is.
        if (value_part.size() >= 2 && value_part.front() == '\'' && value_part.back() == '\'') {
            std::string value = value_part.substr(1, value_part.size() - 2);
            addSqlParameter(value);
            return column + found_operator + placeholder(sql_parameters_.size());
        }

        // For unquoted values keep original format (compatibility with current tests/contract).
        return column + found_operator + value_part;
    } else {
        // If operator not found, Return original condition
        return condition;
    }
}

std::string QueryBuilder::generateSQL() {
    std::ostringstream sql;
    sql_parameters_.clear(); // Clear parameters before generation
    has_sql_parameters_ = false; // Reset Flag

    // Validation name table
    if (!isValidIdentifier(request_data.table)) {
        throw std::runtime_error("Invalid table name: " + request_data.table);
    }

    switch (request_data.method) {
        case HttpMethod::GET: {
            // SELECT request
            sql << "SELECT ";
            if (!request_data.fields.empty()) {
                for (size_t i = 0; i < request_data.fields.size(); ++i) {
                    if (i > 0) sql << ", ";
                    if (!isValidIdentifier(request_data.fields[i])) {
                        throw std::runtime_error("Invalid field name: " + request_data.fields[i]);
                    }
                    sql << request_data.fields[i];
                }
            } else {
                sql << "*";
            }

            sql << " FROM " << request_data.table;

            if (!request_data.joins.empty()) {
                for (const auto &join: request_data.joins) {
                    if (!isValidJoin(join)) {
                        throw std::runtime_error("Invalid JOIN clause: " + join);
                    }
                    sql << " " << join;
                }
            }

            // Handle filters
            if (!request_data.filters.empty()) {
                sql << " WHERE ";
                for (size_t i = 0; i < request_data.filters.size(); ++i) {
                    if (i > 0) sql << " AND ";
                    std::string condition = request_data.filters[i];

                    if (!isValidFilter(condition)) {
                        throw std::runtime_error("Invalid filter: " + condition);
                    }

                    sql << processFilterCondition(condition);
                }
            }

            // Handle GROUP BY
            if (!request_data.groups.empty()) {
                sql << " GROUP BY ";
                for (size_t i = 0; i < request_data.groups.size(); ++i) {
                    if (i > 0) sql << ", ";
                    std::string group = request_data.groups[i];
                    if (!isValidIdentifier(group)) {
                        throw std::runtime_error("Invalid GROUP BY field: " + group);
                    }
                    sql << group;
                }
            }

            // Handle HAVING
            if (!request_data.havings.empty()) {
                sql << " HAVING ";
                for (size_t i = 0; i < request_data.havings.size(); ++i) {
                    if (i > 0) sql << " AND ";
                    std::string having = request_data.havings[i];
                    if (!isValidFilter(having)) {
                        throw std::runtime_error("Invalid HAVING condition: " + having);
                    }
                    sql << processFilterCondition(having);
                }
            }

            // Handle ORDER BY
            if (!request_data.orders.empty()) {
                sql << " ORDER BY ";
                for (size_t i = 0; i < request_data.orders.size(); ++i) {
                    if (i > 0) sql << ", ";
                    std::string order = request_data.orders[i];
                    if (!isValidOrderBy(order)) {
                        throw std::runtime_error("Invalid ORDER BY field: " + order);
                    }
                    sql << order;
                }
            }

            // Handle LIMIT
            if (request_data.limit_value >= 0) {
                sql << " LIMIT " << request_data.limit_value;
            }
            break;
        }

        case HttpMethod::POST: {
            // INSERT request with returning ID
            sql << "INSERT INTO " << request_data.table << " (";

            std::vector<std::string> keys;
            for (const auto &[key, _]: request_data.data) {
                if (!isValidIdentifier(key)) {
                    throw std::runtime_error("Invalid data key: " + std::string(key));
                }
                keys.push_back(key);
            }

            for (size_t i = 0; i < keys.size(); ++i) {
                if (i > 0) sql << ", ";
                sql << keys[i];
            }

            sql << ") VALUES (";

            for (size_t i = 0; i < keys.size(); ++i) {
                if (i > 0) sql << ", ";
                sql << placeholder(i + 1);
                addSqlParameter(jsonValueToSqlParameter(request_data.data.at(keys[i])));
            }

            if (supportsReturning()) {
                sql << ") RETURNING id"; // Return ID created record
            } else {
                sql << ")";
            }
            break;
        }

        case HttpMethod::PATCH:
        case HttpMethod::PUT: {
            // UPDATE request
            sql << "UPDATE " << request_data.table << " SET ";

            std::vector<std::string> keys;
            for (const auto &[key, _]: request_data.data) {
                if (!isValidIdentifier(key)) {
                    throw std::runtime_error("Invalid data key: " + std::string(key));
                }
                keys.push_back(key);
            }

            for (size_t i = 0; i < keys.size(); ++i) {
                if (i > 0) sql << ", ";
                sql << keys[i] << " = " << placeholder(i + 1);
                addSqlParameter(jsonValueToSqlParameter(request_data.data.at(keys[i])));
            }

            // Handle WHERE conditions
            if (!request_data.filters.empty()) {
                sql << " WHERE ";
                for (size_t i = 0; i < request_data.filters.size(); ++i) {
                    if (i > 0) sql << " AND ";
                    std::string condition = request_data.filters[i];

                    if (!isValidFilter(condition)) {
                        throw std::runtime_error("Invalid filter: " + condition);
                    }

                    sql << processFilterCondition(condition);
                }
            }
            break;
        }

        case HttpMethod::DELETE: {
            // DELETE request
            sql << "DELETE FROM " << request_data.table;

            // Handle WHERE conditions
            if (!request_data.filters.empty()) {
                sql << " WHERE ";
                for (size_t i = 0; i < request_data.filters.size(); ++i) {
                    if (i > 0) sql << " AND ";
                    std::string condition = request_data.filters[i];

                    if (!isValidFilter(condition)) {
                        throw std::runtime_error("Invalid filter: " + condition);
                    }

                    sql << processFilterCondition(condition);
                }
            }
            break;
        }
    }

    return sql.str();
}

ResponseData QueryBuilder::buildResponse(const std::vector<std::map<std::string, std::string> > &results) {
    ResponseData response;

    if (request_data.method == HttpMethod::POST) {
        // For POST requests Return successful status
        response.status = boost::beast::http::status::created;  // 201 Created
        response.message = "Record created successfully";
        response.count = 1;

        // If database data supports RETURNING, can get ID
        if (!results.empty()) {
            boost::json::object inserted_record;
            for (const auto &[key, value]: results[0]) {
                inserted_record[key] = value;
            }
            response.data.push_back(inserted_record);
        }
    } else if (request_data.method == HttpMethod::PUT ||
               request_data.method == HttpMethod::PATCH ||
               request_data.method == HttpMethod::DELETE) {
        response.status = boost::beast::http::status::ok;  // 200 OK
        response.message = "Operation completed successfully";
        response.count = 0;
    } else {
        // For SELECT requests
        if (results.empty()) {
            response.status = boost::beast::http::status::not_found;  // 404 Not Found
            response.message = "No data found for the given criteria";
        } else {
            response.status = boost::beast::http::status::ok;  // 200 OK
            response.message = "Query executed successfully";
        }
        response.count = results.size();

        for (const auto &row: results) {
            boost::json::object json_row;
            for (const auto &[key, value]: row) {
                json_row[key] = value;
            }
            response.data.push_back(json_row);
        }
    }

    return response;
}


// Method for getting ResponseData
ResponseData QueryBuilder::getResponse() {
    try {
        std::string sql = generateSQL();
        DatabaseResponse db_response;

        if (hasSqlParameters()) {
            std::vector<std::string> params_copy = getSqlParameters();
            db_response = db_interface->exec(sql, params_copy);
        } else {
            db_response = db_interface->exec(sql);
        }

        // Convert DatabaseResponse in ResponseData
        ResponseData response;
        response.status = db_response.http_status;
        response.message = db_response.message;
        response.count = db_response.count;

        // Convert data in format boost::json
        for (const auto& row : db_response.data) {
            boost::json::object json_row;
            for (const auto& [key, value] : row) {
                json_row[key] = value;
            }
            response.data.push_back(json_row);
        }

        return response;

    } catch (const std::exception &e) {
        ResponseData response;
        response.status = boost::beast::http::status::internal_server_error;
        response.message = e.what();
        response.count = 0;
        return response;
    }
}

// Method for getting JSON-rows
std::string QueryBuilder::getJsonResponse() {
    return getResponse().toJson();
}

void QueryBuilder::printRawData() const {
    request_data.printRawData();
}

ResponseData QueryBuilder::exec() {
    try {
        if (!db_interface || !db_interface->isConnected()) {
            ResponseData response;
            response.status = boost::beast::http::status::service_unavailable;
            response.message = "Database interface is not connected";
            response.count = 0;
            return response;
        }

        std::string sql = generateSQL();
        DatabaseResponse db_response;

        if (hasSqlParameters()) {
            std::vector<std::string> params_copy = getSqlParameters();
            db_response = db_interface->exec(sql, params_copy);
        } else {
            db_response = db_interface->exec(sql);
        }

        // Convert DatabaseResponse in ResponseData
        ResponseData response;
        response.status = db_response.http_status;
        response.message = db_response.message;
        response.count = db_response.count;

        // Convert data in format boost::json
        for (const auto& row : db_response.data) {
            boost::json::object json_row;
            for (const auto& [key, value] : row) {
                json_row[key] = value;
            }
            response.data.push_back(json_row);
        }

        return response;

    } catch (const std::exception &e) {
        ResponseData response;
        response.status = boost::beast::http::status::internal_server_error;
        response.message = e.what();
        response.count = 0;
        return response;
    }
}


#if QORNIX_ENABLE_ASYNC_DB
namespace {

qornix::db::QueryParam asyncQueryParamFromString(const std::string& value) {
    if (value == "NULL") {
        return qornix::db::QueryParam::null();
    }
    return qornix::db::QueryParam::text(value);
}

boost::beast::http::status dbErrorToStatus(const qornix::db::DbError& error) {
    using qornix::db::DbErrorCode;
    switch (error.code()) {
        case DbErrorCode::PoolRejected:
        case DbErrorCode::Connection:
        case DbErrorCode::ConnectionLost:
        case DbErrorCode::Unavailable:
            return boost::beast::http::status::service_unavailable;
        case DbErrorCode::PoolTimeout:
        case DbErrorCode::Timeout:
        case DbErrorCode::Cancelled:
            return boost::beast::http::status::gateway_timeout;
        case DbErrorCode::ConstraintViolation:
        case DbErrorCode::DuplicateKey:
        case DbErrorCode::ForeignKeyViolation:
        case DbErrorCode::Conflict:
            return boost::beast::http::status::conflict;
        case DbErrorCode::Syntax:
        case DbErrorCode::QueryRejected:
            return boost::beast::http::status::bad_request;
        default:
            return boost::beast::http::status::internal_server_error;
    }
}

boost::json::object rowToJsonObject(const qornix::db::Row& row) {
    boost::json::object object;
    for (const auto& [key, value] : row.columns) {
        object[key] = value;
    }
    return object;
}

} // namespace

AsyncQueryBuilder::AsyncQueryBuilder() = default;

AsyncQueryBuilder::AsyncQueryBuilder(std::shared_ptr<AsyncDatabaseInterface> database)
    : database_(std::move(database)) {
    syncDialectOptions();
}

void AsyncQueryBuilder::setDatabaseInterface(std::shared_ptr<AsyncDatabaseInterface> database) {
    database_ = std::move(database);
    syncDialectOptions();
}

AsyncQueryBuilder& AsyncQueryBuilder::setMethod(const std::string& method) {
    builder_.setMethod(method);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::setTable(const std::string& table) {
    builder_.setTable(table);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::setData(const boost::json::object& data) {
    builder_.setData(data);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::addFilter(const std::string& condition) {
    builder_.addFilter(condition);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::addGroupBy(const std::string& field) {
    builder_.addGroupBy(field);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::addOrderBy(const std::string& field) {
    builder_.addOrderBy(field);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::addValue(const std::string& field) {
    builder_.addValue(field);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::addJoin(const std::string& join_clause) {
    builder_.addJoin(join_clause);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::addHaving(const std::string& condition) {
    builder_.addHaving(condition);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::setLimit(int limit) {
    builder_.setLimit(limit);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::queryOptions(qornix::db::QueryOptions options) {
    query_options_ = std::move(options);
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::timeout(std::chrono::milliseconds timeout) {
    query_options_.timeout = timeout;
    return *this;
}

AsyncQueryBuilder& AsyncQueryBuilder::prepared(bool enabled) {
    query_options_.prepared = enabled;
    return *this;
}

void AsyncQueryBuilder::parseRequest(
    const std::string& query_string,
    const std::optional<std::string>& table,
    const std::optional<std::string>& method) {
    builder_.parseRequest(query_string, table, method);
}

std::string AsyncQueryBuilder::generateSQL() {
    syncDialectOptions();
    return builder_.generateSQL();
}

const std::vector<std::string>& AsyncQueryBuilder::getSqlParameters() const {
    return builder_.getSqlParameters();
}

bool AsyncQueryBuilder::hasSqlParameters() const {
    return builder_.hasSqlParameters();
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncQueryBuilder::execute(qornix::db::CancellationToken token) {
    if (!database_) {
        throw std::runtime_error("AsyncQueryBuilder database interface is not configured");
    }
    syncDialectOptions();
    std::string sql = builder_.generateSQL();
    auto params = buildAsyncParams();

    switch (builder_.request_data.method) {
        case HttpMethod::GET: {
            auto result = co_await database_->query(std::move(sql), std::move(params), query_options_, std::move(token));
            co_return result;
        }
        case HttpMethod::POST:
        case HttpMethod::PATCH:
        case HttpMethod::PUT:
        case HttpMethod::DELETE: {
            if (database_->supportsReturning() && builder_.request_data.method == HttpMethod::POST) {
                auto result = co_await database_->executeReturning(std::move(sql), std::move(params), query_options_, std::move(token));
                co_return result;
            }
            auto result = co_await database_->execute(std::move(sql), std::move(params), query_options_, std::move(token));
            co_return result;
        }
    }

    throw std::runtime_error("Unsupported async query builder method");
}

boost::asio::awaitable<ResponseData> AsyncQueryBuilder::exec(qornix::db::CancellationToken token) {
    try {
        auto result = co_await execute(std::move(token));
        co_return buildResponse(result);
    } catch (const qornix::db::DbError& error) {
        ResponseData response;
        response.status = dbErrorToStatus(error);
        response.message = error.what();
        response.count = 0;
        co_return response;
    } catch (const std::exception& error) {
        ResponseData response;
        response.status = boost::beast::http::status::internal_server_error;
        response.message = error.what();
        response.count = 0;
        co_return response;
    }
}

boost::asio::awaitable<std::string> AsyncQueryBuilder::getJsonResponse(qornix::db::CancellationToken token) {
    auto response = co_await exec(std::move(token));
    co_return response.toJson();
}

qornix::db::QueryParams AsyncQueryBuilder::buildAsyncParams() const {
    qornix::db::QueryParams params;
    for (const auto& value : builder_.getSqlParameters()) {
        params.push_back(asyncQueryParamFromString(value));
    }
    return params;
}

ResponseData AsyncQueryBuilder::buildResponse(const qornix::db::QueryResult& result) const {
    ResponseData response;

    if (builder_.request_data.method == HttpMethod::POST) {
        response.status = boost::beast::http::status::created;
        response.message = "Record created successfully";
        response.count = result.rows.empty() ? static_cast<int>(result.affected_rows) : static_cast<int>(result.rows.size());
    } else if (builder_.request_data.method == HttpMethod::PUT ||
               builder_.request_data.method == HttpMethod::PATCH ||
               builder_.request_data.method == HttpMethod::DELETE) {
        response.status = boost::beast::http::status::ok;
        response.message = "Operation completed successfully";
        response.count = static_cast<int>(result.affected_rows);
    } else if (result.rows.empty()) {
        response.status = boost::beast::http::status::not_found;
        response.message = "No data found for the given criteria";
        response.count = 0;
    } else {
        response.status = boost::beast::http::status::ok;
        response.message = "Query executed successfully";
        response.count = static_cast<int>(result.rows.size());
    }

    for (const auto& row : result.rows) {
        response.data.push_back(rowToJsonObject(row));
    }
    return response;
}

void AsyncQueryBuilder::syncDialectOptions() {
    if (!database_) {
        builder_.setPlaceholderFormatter({});
        builder_.setSupportsReturning(true);
        return;
    }
    auto database = database_;
    builder_.setPlaceholderFormatter([database](std::size_t index) {
        return database->placeholder(index);
    });
    builder_.setSupportsReturning(database_->supportsReturning());
}
#endif
