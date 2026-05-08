/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
// MySQLDriver.cpp
#include "MySQLDriver.h"

#include <mysql/mysql.h>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cctype>

MySQLDriver::MySQLDriver()
    : conn_(nullptr), connected_(false) {
    std::cout << "[MySQLDriver] Initialized" << std::endl;
}

MySQLDriver::~MySQLDriver() {
    if (connected_) {
        disconnect();
    }
}

void MySQLDriver::parseConnectionString(const std::string &cs, std::string &host, std::string &user,
                                        std::string &password, std::string &db, unsigned int &port,
                                        std::string &unix_socket) {
    // Podderzhivaem and "key=value;key2=value2", and "key=value key2=value2"
    host.clear();
    user.clear();
    password.clear();
    db.clear();
    unix_socket.clear();
    port = 3306;

    std::string normalized = cs;
    std::replace(normalized.begin(), normalized.end(), ';', ' ');
    std::istringstream iss(normalized);
    std::string token;

    auto trim = [](std::string &s) {
        size_t a = 0;
        while (a < s.size() && isspace((unsigned char) s[a])) ++a;
        size_t b = s.size();
        while (b > a && isspace((unsigned char) s[b - 1])) --b;
        s = s.substr(a, b - a);
    };

    while (iss >> token) {
        size_t eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = token.substr(0, eq);
        std::string value = token.substr(eq + 1);
        trim(key);
        trim(value);

        if (key == "host") host = value;
        else if (key == "user" || key == "username") user = value;
        else if (key == "password") password = value;
        else if (key == "db" || key == "database" || key == "dbname") db = value;
        else if (key == "port") {
            try { port = static_cast<unsigned int>(std::stoul(value)); } catch (...) { port = 3306; }
        } else if (key == "unix_socket" || key == "socket") unix_socket = value;
    }
}

std::string MySQLDriver::buildEscapedQueryWithParams(const std::string& query, const std::vector<std::string>& params) {
    if (!conn_) {
        throw ConnectionError("MySQL: not connected");
    }

    std::string result = query;
    for (size_t i = 0; i < params.size(); ++i) {
        const std::string placeholder = "$" + std::to_string(i + 1);
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            std::string escaped;
            escaped.resize(params[i].size() * 2 + 1);
            const auto escaped_len = mysql_real_escape_string(
                conn_, escaped.data(), params[i].c_str(), static_cast<unsigned long>(params[i].size()));
            escaped.resize(escaped_len);
            const std::string quoted = "'" + escaped + "'";
            result.replace(pos, placeholder.length(), quoted);
            pos += quoted.length();
        }
    }
    return result;
}

void MySQLDriver::createDatabaseIfNeeded(const std::string& host, const std::string& user, const std::string& password,
                                         const std::string& db, unsigned int port, const std::string& unix_socket) {
    if (db.empty()) {
        throw DatabaseError("MySQL database name is empty");
    }

    MYSQL* admin_conn = mysql_init(nullptr);
    if (!admin_conn) {
        throw ConnectionError("MySQL: failed to init mysql handle for DB creation");
    }

    const char* host_c = host.empty() ? nullptr : host.c_str();
    const char* user_c = user.empty() ? nullptr : user.c_str();
    const char* pass_c = password.empty() ? nullptr : password.c_str();
    const char* socket_c = unix_socket.empty() ? nullptr : unix_socket.c_str();

    if (!mysql_real_connect(admin_conn, host_c, user_c, pass_c, nullptr, port, socket_c, 0)) {
        std::string err = mysql_error(admin_conn);
        mysql_close(admin_conn);
        throw ConnectionError(std::string("MySQL admin connection error: ") + err);
    }

    std::string create_db_sql = "CREATE DATABASE IF NOT EXISTS `" + db + "`";
    if (mysql_query(admin_conn, create_db_sql.c_str()) != 0) {
        std::string err = mysql_error(admin_conn);
        mysql_close(admin_conn);
        throw DatabaseError(std::string("Failed to create MySQL database: ") + err);
    }

    mysql_close(admin_conn);
}

DatabaseResponse MySQLDriver::exec(const std::string& query, const std::vector<std::string>& params) {
    DatabaseResponse response;

    if (!connected_ || !conn_) {
        response.http_status = boost::beast::http::status::internal_server_error;
        response.message = "Database not connected";
        return response;
    }

    std::string executable_query = query;
    if (!params.empty()) {
        executable_query = buildEscapedQueryWithParams(query, params);
    }

    if (mysql_query(conn_, executable_query.c_str()) != 0) {
        std::string err = mysql_error(conn_);
        auto classifiedError = classifyDatabaseError(err);

        if (dynamic_cast<NotFoundError*>(classifiedError.get())) {
            response.http_status = boost::beast::http::status::not_found;
            response.message = "Resource not found";
        } else if (dynamic_cast<ValidationError*>(classifiedError.get())) {
            response.http_status = boost::beast::http::status::bad_request;
            response.message = "Validation error: " + std::string(classifiedError->what());
        } else if (dynamic_cast<ConnectionError*>(classifiedError.get())) {
            response.http_status = boost::beast::http::status::service_unavailable;
            response.message = "Database connection error";
        } else {
            response.http_status = boost::beast::http::status::internal_server_error;
            response.message = "Internal server error: " + std::string(classifiedError->what());
        }
        return response;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (res) {
        response.data = parseResultSet(res);
        response.count = static_cast<int>(response.data.size());
        response.response_has_header = true;

        if (response.data.empty()) {
            response.http_status = boost::beast::http::status::not_found;
            response.message = "No data found for the given criteria";
        } else {
            response.http_status = boost::beast::http::status::ok;
            response.message = "Query executed successfully";
        }
        mysql_free_result(res);
    } else {
        if (mysql_field_count(conn_) != 0) {
            response.http_status = boost::beast::http::status::internal_server_error;
            response.message = "Failed to read result set";
            return response;
        }

        response.affected_rows = static_cast<int>(mysql_affected_rows(conn_));
        if (response.affected_rows > 0) {
            response.http_status = boost::beast::http::status::ok;
            response.message = "Operation completed successfully";
        } else {
            response.http_status = boost::beast::http::status::not_found;
            response.message = "No records found matching the criteria";
        }
    }

    return response;
}

std::vector<std::map<std::string, std::string>> MySQLDriver::parseResultSet(MYSQL_RES* res) {
    std::vector<std::map<std::string, std::string>> result;

    MYSQL_ROW row;
    unsigned int num_fields = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);

    while ((row = mysql_fetch_row(res))) {
        std::map<std::string, std::string> rowMap;
        for (unsigned int i = 0; i < num_fields; ++i) {
            std::string fieldName = fields[i].name;
            std::string fieldValue = (row[i]) ? row[i] : "NULL";
            rowMap[fieldName] = fieldValue;
        }
        result.push_back(rowMap);
    }

    return result;
}

std::string MySQLDriver::executeQueryWithHeaders(const std::string &query) {
    if (!connected_ || !conn_) {
        throw ConnectionError("MySQL: not connected");
    }

    if (mysql_query(conn_, query.c_str()) != 0) {
        std::string err = mysql_error(conn_);
        auto classifiedError = classifyDatabaseError(err);

        if (dynamic_cast<NotFoundError*>(classifiedError.get())) {
            throw NotFoundError(classifiedError->what());
        } else if (dynamic_cast<ValidationError*>(classifiedError.get())) {
            throw ValidationError(classifiedError->what());
        } else if (dynamic_cast<ConnectionError*>(classifiedError.get())) {
            throw ConnectionError(classifiedError->what());
        } else if (dynamic_cast<ServerError*>(classifiedError.get())) {
            throw ServerError(classifiedError->what());
        } else {
            throw DatabaseError(classifiedError->what());
        }
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        return std::string();
    }

    MYSQL_ROW row;
    unsigned int num_fields = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    std::ostringstream out;

    // Output names columns
    for (unsigned int i = 0; i < num_fields; ++i) {
        out << fields[i].name;
        if (i + 1 < num_fields) out << '\t';
    }

    unsigned long long numRows = mysql_num_rows(res);
    if (numRows > 0) {
        out << '\n';
    }

    // Output rows data
    unsigned long long row_index = 0;
    while ((row = mysql_fetch_row(res))) {
        for (unsigned int i = 0; i < num_fields; ++i) {
            if (row[i]) {
                out << row[i];
            } else {
                out << "NULL";
            }
            if (i + 1 < num_fields) out << '\t';
        }
        row_index++;
        if (row_index < numRows) {
            out << '\n';
        }
    }

    mysql_free_result(res);
    return out.str();
}

std::string MySQLDriver::executeQueryWithParams(const std::string& query, const std::vector<std::string>& params) {
    if (!connected_ || !conn_) {
        throw ConnectionError("MySQL: not connected");
    }

    std::string executable_query = buildEscapedQueryWithParams(query, params);
    return executeQueryWithHeaders(executable_query);
}

std::string MySQLDriver::executeQuery(const std::string &query, const std::string &dbSchema) {
    if (!connected_ || !conn_) {
        throw ConnectionError("MySQL: not connected");
    }

    if (mysql_query(conn_, query.c_str()) != 0) {
        std::string err = mysql_error(conn_);
        auto classifiedError = classifyDatabaseError(err);

        if (dynamic_cast<NotFoundError*>(classifiedError.get())) {
            throw NotFoundError(classifiedError->what());
        } else if (dynamic_cast<ValidationError*>(classifiedError.get())) {
            throw ValidationError(classifiedError->what());
        } else if (dynamic_cast<ConnectionError*>(classifiedError.get())) {
            throw ConnectionError(classifiedError->what());
        } else if (dynamic_cast<ServerError*>(classifiedError.get())) {
            throw ServerError(classifiedError->what());
        } else {
            throw DatabaseError(classifiedError->what());
        }
    }

    MYSQL_RES *res = mysql_store_result(conn_);
    if (!res) {
        return std::string();
    }

    MYSQL_ROW row;
    unsigned int num_fields = mysql_num_fields(res);
    std::ostringstream out;
    unsigned long long numRows = mysql_num_rows(res);
    unsigned long long row_index = 0;

    while ((row = mysql_fetch_row(res))) {
        for (unsigned int i = 0; i < num_fields; ++i) {
            if (row[i]) out << row[i];
            else out << "NULL";
            if (i + 1 < num_fields) out << '\t';
        }
        row_index++;
        if (row_index < numRows) {
            out << '\n';
        }
    }

    mysql_free_result(res);
    return out.str();
}

int MySQLDriver::executeNonQuery(const std::string &query) {
    if (!connected_ || !conn_) {
        throw ConnectionError("MySQL: not connected");
    }

    if (mysql_query(conn_, query.c_str()) != 0) {
        std::string err = mysql_error(conn_);
        auto classifiedError = classifyDatabaseError(err);

        if (dynamic_cast<NotFoundError*>(classifiedError.get())) {
            throw NotFoundError(classifiedError->what());
        } else if (dynamic_cast<ValidationError*>(classifiedError.get())) {
            throw ValidationError(classifiedError->what());
        } else if (dynamic_cast<ConnectionError*>(classifiedError.get())) {
            throw ConnectionError(classifiedError->what());
        } else if (dynamic_cast<ServerError*>(classifiedError.get())) {
            throw ServerError(classifiedError->what());
        } else {
            throw DatabaseError(classifiedError->what());
        }
    }

    my_ulonglong affected = mysql_affected_rows(conn_);
    return static_cast<int>(affected);
}

int MySQLDriver::executeUpdate(const std::string &query) {
    if (!connected_ || !conn_) {
        throw ConnectionError("MySQL: not connected");
    }

    if (mysql_query(conn_, query.c_str()) != 0) {
        std::string err = mysql_error(conn_);
        auto classifiedError = classifyDatabaseError(err);

        if (dynamic_cast<NotFoundError*>(classifiedError.get())) {
            throw NotFoundError(classifiedError->what());
        } else if (dynamic_cast<ValidationError*>(classifiedError.get())) {
            throw ValidationError(classifiedError->what());
        } else if (dynamic_cast<ConnectionError*>(classifiedError.get())) {
            throw ConnectionError(classifiedError->what());
        } else if (dynamic_cast<ServerError*>(classifiedError.get())) {
            throw ServerError(classifiedError->what());
        } else {
            throw DatabaseError(classifiedError->what());
        }
    }

    my_ulonglong affected = mysql_affected_rows(conn_);
    return static_cast<int>(affected);
}

bool MySQLDriver::isConnected() const {
    if (!connected_ || !conn_) {
        return false;
    }
    return mysql_ping(conn_) == 0;
}

std::vector<std::string> MySQLDriver::getTableNames() {
    if (!isConnected()) {
        throw ConnectionError("Database not connected");
    }

    const std::string query =
        "SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES "
        "WHERE TABLE_SCHEMA = DATABASE() ORDER BY TABLE_NAME";

    std::string raw_result = executeQuery(query);
    std::vector<std::string> table_names;
    std::istringstream result_stream(raw_result);
    std::string line;
    while (std::getline(result_stream, line)) {
        if (!line.empty()) {
            table_names.push_back(line);
        }
    }
    return table_names;
}

std::vector<std::string> MySQLDriver::getColumnNames(const std::string &table_name) {
    if (!isConnected()) {
        throw ConnectionError("Database not connected");
    }

    std::string query = "SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
                        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '" + table_name + "' "
                        "ORDER BY ORDINAL_POSITION";

    try {
        std::string raw_result = executeQuery(query);

        std::vector<std::string> column_names;
        std::istringstream result_stream(raw_result);
        std::string line;

        while (std::getline(result_stream, line)) {
            column_names.push_back(line);
        }

        return column_names;
    } catch (const NotFoundError& e) {
        return {};
    }
}

std::unique_ptr<DatabaseError> MySQLDriver::classifyDatabaseError(const std::string& errorMessage) {
    std::string lowerMsg = errorMessage;
    std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);

    // Check error "not found"
    if ((lowerMsg.find("doesn't exist") != std::string::npos ||
         lowerMsg.find("does not exist") != std::string::npos ||
         lowerMsg.find("unknown column") != std::string::npos ||
         lowerMsg.find("no data found") != std::string::npos ||
         lowerMsg.find("record not found") != std::string::npos ||
         lowerMsg.find("table doesn't exist") != std::string::npos ||
         lowerMsg.find("table does not exist") != std::string::npos)) {
        return std::make_unique<NotFoundError>(errorMessage);
    }

    // Check error validation
    if (lowerMsg.find("foreign key constraint fails") != std::string::npos ||
        lowerMsg.find("cannot add or update a child row") != std::string::npos ||
        lowerMsg.find("column '") != std::string::npos && lowerMsg.find("cannot be null") != std::string::npos ||
        lowerMsg.find("duplicate entry") != std::string::npos ||
        lowerMsg.find("violates not-null constraint") != std::string::npos ||
        lowerMsg.find("invalid input syntax") != std::string::npos ||
        lowerMsg.find("truncated incorrect") != std::string::npos ||
        lowerMsg.find("data too long") != std::string::npos) {
        return std::make_unique<ValidationError>(errorMessage);
    }

    // All remaining error treat as server-side
    return std::make_unique<ServerError>(errorMessage);
}

void MySQLDriver::connect(const std::string &connectionString) {
    connectionString_ = connectionString;

    std::string host, user, password, db, unix_socket;
    unsigned int port = 0;
    parseConnectionString(connectionString_, host, user, password, db, port, unix_socket);

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        throw ConnectionError("MySQL: failed to init mysql handle");
    }

    // Set optsionalnye taymauty and flagi pri need

    // If host empty, pass nullptr so as not to use localhost/pipe
    const char *host_c = host.empty() ? nullptr : host.c_str();
    const char *user_c = user.empty() ? nullptr : user.c_str();
    const char *pass_c = password.empty() ? nullptr : password.c_str();
    const char *db_c = db.empty() ? nullptr : db.c_str();
    const char *socket_c = unix_socket.empty() ? nullptr : unix_socket.c_str();

    if (!mysql_real_connect(conn_, host_c, user_c, pass_c, db_c, port, socket_c, 0)) {
        std::string err = mysql_error(conn_);
        const unsigned int code = mysql_errno(conn_);

        // Unknown database: Create DB and povtoryaem connection.
        if (code == 1049 && !db.empty()) {
            mysql_close(conn_);
            conn_ = nullptr;

            createDatabaseIfNeeded(host, user, password, db, port, unix_socket);

            conn_ = mysql_init(nullptr);
            if (!conn_) {
                throw ConnectionError("MySQL: failed to re-init mysql handle");
            }
            if (!mysql_real_connect(conn_, host_c, user_c, pass_c, db_c, port, socket_c, 0)) {
                std::string second_err = mysql_error(conn_);
                mysql_close(conn_);
                conn_ = nullptr;
                throw ConnectionError(std::string("MySQL connection error after DB creation: ") + second_err);
            }
        } else {
            mysql_close(conn_);
            conn_ = nullptr;
            throw ConnectionError(std::string("MySQL connection error: ") + err);
        }
    }

    currentDatabase_ = db;
    connected_ = true;
    std::cout << "[MySQLDriver] Connected" << std::endl;
}

void MySQLDriver::disconnect() {
    if (!conn_) return;
    mysql_close(conn_);
    conn_ = nullptr;
    connected_ = false;
    std::cout << "[MySQLDriver] Disconnected" << std::endl;
}
