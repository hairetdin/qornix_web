/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
// Implementation PostgreSQLDriver: uses libpq (libpq-fe.h)
#include "PostgreSQLDriver.h"

#include <libpq-fe.h>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

PostgreSQLDriver::PostgreSQLDriver()
    : conn_(nullptr), connected_(false) {
    std::cout << "[PostgreSQLDriver] Initialized" << std::endl;
}

PostgreSQLDriver::~PostgreSQLDriver() {
    if (connected_) {
        disconnect();
    }
}

DatabaseResponse PostgreSQLDriver::exec(const std::string& query, const std::vector<std::string>& params) {
    DatabaseResponse response;

    if (!connected_ || !conn_) {
        response.http_status = boost::beast::http::status::internal_server_error;
        response.message = "Database not connected";
        return response;
    }

    PGresult *res;

    if (!params.empty()) {
        std::vector<const char*> param_values(params.size());
        for (size_t i = 0; i < params.size(); ++i) {
            param_values[i] = params[i].c_str();
        }
        res = PQexecParams(conn_, query.c_str(), params.size(), nullptr,
                          param_values.data(), nullptr, nullptr, 0);
    } else {
        res = PQexec(conn_, query.c_str());
    }

    if (!res) {
        response.http_status = boost::beast::http::status::internal_server_error;
        response.message = "Failed to execute query";
        return response;
    }

    ExecStatusType status = PQresultStatus(res);

    try {
        if (status == PGRES_TUPLES_OK || status == PGRES_SINGLE_TUPLE) {
            // SELECT request - Return data
            response.data = parseResultSet(res);
            response.count = response.data.size();

            // For SELECT requests: if no data, Return 404
            if (response.data.empty()) {
                response.http_status = boost::beast::http::status::not_found;
                response.message = "No data found for the given criteria";
            } else {
                response.http_status = boost::beast::http::status::ok;
                response.message = "Query executed successfully";
            }
            response.response_has_header = true;
        } else if (status == PGRES_COMMAND_OK) {
            // INSERT/UPDATE/DELETE request - Return number of affected rows
            char *tuples = PQcmdTuples(res);
            response.affected_rows = (tuples && tuples[0] != '\0') ? std::atoi(tuples) : 0;

            if (response.affected_rows > 0) {
                response.http_status = boost::beast::http::status::ok;
                response.message = "Operation completed successfully";
            } else {
                response.http_status = boost::beast::http::status::not_found;
                response.message = "No records found matching the criteria";
            }
        } else {
            // Error execution
            std::string err = PQresultErrorMessage(res) ? PQresultErrorMessage(res) : "unknown error";
            auto classifiedError = classifyDatabaseError(err);

            // Set HTTP status in depending from type error
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
        }
    } catch (const std::exception& e) {
        response.http_status = boost::beast::http::status::internal_server_error;
        response.message = "Internal server error: " + std::string(e.what());
    }

    PQclear(res);
    return response;
}

std::vector<std::map<std::string, std::string>> PostgreSQLDriver::parseResultSet(PGresult* res) {
    std::vector<std::map<std::string, std::string>> result;

    int nrows = PQntuples(res);
    int ncols = PQnfields(res);

    // Get names columns
    std::vector<std::string> headers;
    for (int c = 0; c < ncols; ++c) {
        headers.push_back(PQfname(res, c));
    }

    // Parse rows data
    for (int r = 0; r < nrows; ++r) {
        std::map<std::string, std::string> row;
        for (int c = 0; c < ncols; ++c) {
            char *val = PQgetvalue(res, r, c);
            row[headers[c]] = (val) ? val : "NULL";
        }
        result.push_back(row);
    }

    return result;
}

void PostgreSQLDriver::createDatabase(const std::string &connectionString) {
    std::string dbName;
    size_t dbnamePos = connectionString.find("dbname=");

    if (dbnamePos != std::string::npos) {
        size_t start = dbnamePos + 7; // dlina "dbname="
        size_t end = connectionString.find_first_of(" \t", start);
        if (end == std::string::npos) {
            end = connectionString.length();
        }
        dbName = connectionString.substr(start, end - start);
        std::cout << "[PostgreSQLDriver] Creating database: " << dbName << std::endl;

        // Razbiraem connection string on parameters
        std::vector<std::string> params;
        std::istringstream iss(connectionString);
        std::string param;

        while (iss >> param) {
            // Skip dbname parameter
            if (param.find("dbname=") == std::string::npos) {
                params.push_back(param);
            }
        }

        // Build new connection string with dbname=postgres
        std::string tempConnectionString;
        for (const auto &p: params) {
            if (!tempConnectionString.empty()) {
                tempConnectionString += " ";
            }
            tempConnectionString += p;
        }

        // Add dbname=postgres
        if (!tempConnectionString.empty()) {
            tempConnectionString += " ";
        }
        tempConnectionString += "dbname=postgres";

        std::cout << "[PostgreSQLDriver] Temp connection string: " << tempConnectionString << std::endl;

        // Podklyuchaemsya to serveru through sistemnuyu database
        PGconn *tempConn = PQconnectdb(tempConnectionString.c_str());
        if (!tempConn || PQstatus(tempConn) != CONNECTION_OK) {
            std::string err = PQerrorMessage(tempConn) ? PQerrorMessage(tempConn) : "unknown error";
            if (tempConn) PQfinish(tempConn);
            throw ConnectionError(std::string("Failed to connect to server for DB creation: ") + err);
        }

        // Create database
        std::string createDbQuery = "CREATE DATABASE \"" + dbName + "\"";
        PGresult *res = PQexec(tempConn, createDbQuery.c_str());

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string err = PQresultErrorMessage(res) ? PQresultErrorMessage(res) : "unknown error";
            PQclear(res);
            PQfinish(tempConn);
            throw DatabaseError(std::string("Failed to create database: ") + err);
        }

        PQclear(res);
        PQfinish(tempConn);
        std::cout << "[PostgreSQLDriver] Database '" << dbName << "' created successfully" << std::endl;
    } else {
        throw DatabaseError("Database name not found in connection string");
    }
}

void PostgreSQLDriver::connect(const std::string &connectionString) {
    connectionString_ = connectionString;
    conn_ = PQconnectdb(connectionString_.c_str());

    if (!conn_) {
        throw ConnectionError("PostgreSQL: failed to allocate PGconn");
    }

    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn_) ? PQerrorMessage(conn_) : "unknown error";
        std::cout << "[PostgreSQLDriver] Connection failed: " << err << std::endl;

        // Try create database
        try {
            std::cout << "[PostgreSQLDriver] Trying to create database..." << std::endl;
            createDatabase(connectionString_);

            // After creation try snova podklyuchitsya
            PQfinish(conn_);
            conn_ = PQconnectdb(connectionString_.c_str());

            if (PQstatus(conn_) != CONNECTION_OK) {
                std::string secondErr = PQerrorMessage(conn_) ? PQerrorMessage(conn_) : "unknown error";
                PQfinish(conn_);
                conn_ = nullptr;
                throw ConnectionError(std::string("PostgreSQL connection error after DB creation: ") + secondErr);
            }
        } catch (const std::exception &e) {
            PQfinish(conn_);
            conn_ = nullptr;
            throw;
        }
    }

    connected_ = true;
    std::cout << "[PostgreSQLDriver] Connected" << std::endl;
}

void PostgreSQLDriver::disconnect() {
    if (!conn_) return;
    PQfinish(conn_);
    conn_ = nullptr;
    connected_ = false;
    std::cout << "[PostgreSQLDriver] Disconnected" << std::endl;
}

// Helper Method for opredeleniya type error by soobscheniyu
std::unique_ptr<DatabaseError> PostgreSQLDriver::classifyDatabaseError(const std::string& errorMessage) {
    std::string lowerMsg = errorMessage;
    std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);

    // Check error "not found"
    if ((lowerMsg.find("отношение") != std::string::npos &&
         lowerMsg.find("не существует") != std::string::npos) ||
        (lowerMsg.find("relation") != std::string::npos &&
         lowerMsg.find("does not exist") != std::string::npos) ||
        lowerMsg.find("no data found") != std::string::npos ||
        lowerMsg.find("record not found") != std::string::npos) {
        return std::make_unique<NotFoundError>(errorMessage);
        }

    // Check error validation
    if (lowerMsg.find("violates foreign key constraint") != std::string::npos ||
        lowerMsg.find("нарушает ограничение внешнего ключа") != std::string::npos ||
        lowerMsg.find("violates not-null constraint") != std::string::npos ||
        lowerMsg.find("нарушает ограничение not null") != std::string::npos ||
        lowerMsg.find("duplicate key value") != std::string::npos ||
        lowerMsg.find("повторяющееся значение ключа") != std::string::npos ||
        lowerMsg.find("invalid input syntax") != std::string::npos ||
        lowerMsg.find("неверный синтаксис ввода") != std::string::npos) {
        return std::make_unique<ValidationError>(errorMessage);
        }

    // All remaining error treat as server-side
    return std::make_unique<ServerError>(errorMessage);
}

std::string PostgreSQLDriver::executeQueryWithHeaders(const std::string &query) {
    if (!connected_ || !conn_) {
        throw ConnectionError("PostgreSQL: not connected");
    }

    PGresult *res = PQexec(conn_, query.c_str());
    if (!res) {
        throw DatabaseError("PostgreSQL: failed to execute query (no result)");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_SINGLE_TUPLE) {
        std::string err = PQresultErrorMessage(res) ? PQresultErrorMessage(res) : "unknown";
        PQclear(res);
        auto classifiedError = classifyDatabaseError(err);

        // Throw correct Type exceptions
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

    int nrows = PQntuples(res);
    int ncols = PQnfields(res);
    std::ostringstream out;

    // Step 1: output names columns (first string)
    for (int c = 0; c < ncols; ++c) {
        out << PQfname(res, c); // Name column
        if (c + 1 < ncols) out << '\t';
    }
    if (nrows > 0) {
        // If is data, Add newline rows
        out << '\n';
    }

    // Step 2: output rows data (as in original method)
    for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
            char *val = PQgetvalue(res, r, c);
            if (val) {
                out << val;
            } else {
                out << "NULL";
            }
            if (c + 1 < ncols) out << '\t';
        }
        if (r + 1 < nrows) out << '\n';
    }

    PQclear(res);
    return out.str();
}

std::string PostgreSQLDriver::executeQueryWithParams(const std::string& query, const std::vector<std::string>& params) {
    if (!connected_ || !conn_) {
        throw ConnectionError("PostgreSQL: not connected");
    }

    // Convert std::vector<std::string> in array const char*
    std::vector<const char*> param_values(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        param_values[i] = params[i].c_str();
    }

    // Execute parameterized request
    PGresult* res = PQexecParams(conn_, query.c_str(), params.size(), nullptr,
                                 param_values.data(), nullptr, nullptr, 0);

    if (!res) {
        throw DatabaseError("PostgreSQL: failed to execute parameterized query (no result)");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_SINGLE_TUPLE) {
        std::string err = PQresultErrorMessage(res) ? PQresultErrorMessage(res) : "unknown";
        PQclear(res);
        throw DatabaseError(err);
    }

    // Process result
    int nrows = PQntuples(res);
    int ncols = PQnfields(res);
    std::ostringstream out;

    // Step 1: output names columns (first string)
    for (int c = 0; c < ncols; ++c) {
        out << PQfname(res, c); // Name column
        if (c + 1 < ncols) out << '\t';
    }
    if (nrows > 0) {
        // If is data, Add newline rows
        out << '\n';
    }

    // Step 2: output rows data (as in original method)

    for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
            char* val = PQgetvalue(res, r, c);
            if (val) {
                out << val;
            } else {
                out << "NULL";
            }
            if (c + 1 < ncols) out << '\t';
        }
        if (r + 1 < nrows) out << '\n';
    }

    PQclear(res);
    return out.str();
}

std::string PostgreSQLDriver::executeQuery(const std::string &query, const std::string &dbSchema) {
    if (!connected_ || !conn_) {
        throw ConnectionError("PostgreSQL: not connected");
    }

    PGresult *res = PQexec(conn_, query.c_str());
    if (!res) {
        throw DatabaseError("PostgreSQL: failed to execute query (no result)");
    }

    ExecStatusType status = PQresultStatus(res);
    // std::cout << "DEBUG: PostgreSQL query result: " << res << ", status: " << status << std::endl;
    if (status != PGRES_TUPLES_OK && status != PGRES_SINGLE_TUPLE) {
        std::string err = PQresultErrorMessage(res) ? PQresultErrorMessage(res) : "unknown";
        // std::cout << "DEBUG: PostgreSQL query error: " << err << std::endl;
        PQclear(res);
        auto classifiedError = classifyDatabaseError(err);
        // std::cout << "DEBUG: Throwing classified error of type: " << typeid(*classifiedError).name() << std::endl;

        // Throw correct Type exceptions
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

    int nrows = PQntuples(res);
    int ncols = PQnfields(res);
    std::ostringstream out;

    for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
            char *val = PQgetvalue(res, r, c);
            if (val) {
                out << val;
            } else {
                out << "NULL";
            }
            if (c + 1 < ncols) out << '\t';
        }
        if (r + 1 < nrows) out << '\n';
    }

    PQclear(res);
    return out.str();
}

int PostgreSQLDriver::executeNonQuery(const std::string &query) {
    if (!connected_ || !conn_) {
        throw ConnectionError("PostgreSQL: not connected");
    }

    PGresult *res = PQexec(conn_, query.c_str());
    if (!res) {
        throw DatabaseError("PostgreSQL: failed to execute non-query (no result)");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK) {
        std::string err = PQresultErrorMessage(res) ? PQresultErrorMessage(res) : "unknown";
        PQclear(res);
        auto classifiedError = classifyDatabaseError(err);

        // Throw correct Type exceptions
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

    // PQcmdTuples returns chislo affected rows in vide rows
    char *tuples = PQcmdTuples(res);
    int affected = 0;
    if (tuples && tuples[0] != '\0') {
        affected = std::atoi(tuples);
    }

    PQclear(res);
    return affected;
}

bool PostgreSQLDriver::isConnected() const {
    return connected_ && conn_ && PQstatus(const_cast<PGconn *>(conn_)) == CONNECTION_OK;
}

std::vector<std::string> PostgreSQLDriver::getTableNames() {
    if (!isConnected()) {
        throw ConnectionError("Database not connected");
    }

    const std::string query =
        "SELECT tablename FROM pg_catalog.pg_tables "
        "WHERE schemaname = 'public' ORDER BY tablename";

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

std::vector<std::string> PostgreSQLDriver::getColumnNames(const std::string &table_name) {
    if (!isConnected()) {
        throw ConnectionError("Database not connected");
    }

    std::string query = "SELECT column_name FROM information_schema.columns "
                        "WHERE table_name = '" + table_name + "' "
                        "ORDER BY ordinal_position";

    try {
        std::string raw_result = executeQuery(query);

        std::vector<std::string> column_names;
        std::istringstream result_stream(raw_result);
        std::string line;

        while (std::getline(result_stream, line)) {
            // Assume, that each string contains one Name column
            column_names.push_back(line);
        }

        return column_names;
    } catch (const NotFoundError& e) {
        // If table not found, Return empty vector vmesto error
        return {};
    }
}

int PostgreSQLDriver::executeUpdate(const std::string &query) {
    if (!isConnected()) {
        throw ConnectionError("Database not connected");
    }

    PGresult *res = PQexec(conn_, query.c_str());
    ExecStatusType status = PQresultStatus(res);

    if (status != PGRES_COMMAND_OK) {
        std::string error_msg = PQresultErrorMessage(res);
        PQclear(res);
        auto classifiedError = classifyDatabaseError(error_msg);

        // Throw correct Type exceptions
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

    // Get number of affected rows
    int affected_rows = atoi(PQcmdTuples(res));
    PQclear(res);
    return affected_rows;
}
