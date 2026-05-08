/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
// Implementation SQLiteDriver: uses sqlite3.h
#include "SQLiteDriver.h"

#include <sqlite3.h>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <regex>

SQLiteDriver::SQLiteDriver()
    : db_(nullptr), connected_(false) {
    std::cout << "[SQLiteDriver] Initialized" << std::endl;
    std::cout << "[SQLiteDriver] SQLite version: " << sqlite3_libversion() << std::endl;
}

SQLiteDriver::~SQLiteDriver() {
    if (connected_) {
        disconnect();
    }
}

// Check support RETURNING (SQLite 3.35+)
bool SQLiteDriver::supportsReturning() const {
    int version = sqlite3_libversion_number();
    // SQLite 3.35.0 = 3035000
    return version >= 3035000;
}

// Helper Method for extraction name table from INSERT request
std::string SQLiteDriver::extractTableNameFromInsert(const std::string& query) const {
    std::regex tableRegex(R"(INSERT\s+INTO\s+(\w+))", std::regex_constants::icase);
    std::smatch match;
    if (std::regex_search(query, match, tableRegex) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

// Helper Method for getting data by ID
std::vector<std::map<std::string, std::string>> SQLiteDriver::fetchLastInsertedRow(
    const std::string& tableName,
    int64_t lastId) const
{
    std::string query = "SELECT * FROM \"" + tableName + "\" WHERE rowid = ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw DatabaseError(std::string("SQLite prepare error: ") + sqlite3_errmsg(db_));
    }

    rc = sqlite3_bind_int64(stmt, 1, lastId);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("SQLite bind error: ") + sqlite3_errmsg(db_));
    }

    auto result = parseResultSet(stmt);
    sqlite3_finalize(stmt);
    return result;
}

// Helper Method for classification errors
std::unique_ptr<DatabaseError> SQLiteDriver::classifyDatabaseError(const std::string &errorMessage) {
    std::string lowerMsg = errorMessage;
    std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);

    // Check error "not found"
    if (lowerMsg.find("no such table") != std::string::npos ||
        lowerMsg.find("no such column") != std::string::npos ||
        lowerMsg.find("does not exist") != std::string::npos ||
        lowerMsg.find("no data found") != std::string::npos ||
        lowerMsg.find("record not found") != std::string::npos) {
        return std::make_unique<NotFoundError>(errorMessage);
    }

    // Check error validation
    if (lowerMsg.find("constraint failed") != std::string::npos ||
        lowerMsg.find("foreign key constraint failed") != std::string::npos ||
        lowerMsg.find("not null constraint failed") != std::string::npos ||
        lowerMsg.find("unique constraint failed") != std::string::npos ||
        lowerMsg.find("duplicate key") != std::string::npos ||
        lowerMsg.find("datatype mismatch") != std::string::npos) {
        return std::make_unique<ValidationError>(errorMessage);
    }

    // All remaining error treat as server-side
    return std::make_unique<ServerError>(errorMessage);
}

// Helper Method for parsing results
std::vector<std::map<std::string, std::string>> SQLiteDriver::parseResultSet(sqlite3_stmt* stmt) const {
    std::vector<std::map<std::string, std::string>> result;

    int cols = sqlite3_column_count(stmt);

    // Get names columns
    std::vector<std::string> headers;
    for (int c = 0; c < cols; ++c) {
        headers.push_back(sqlite3_column_name(stmt, c));
    }

    // Parse rows data
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        std::map<std::string, std::string> row;
        for (int c = 0; c < cols; ++c) {
            const unsigned char *val = sqlite3_column_text(stmt, c);
            row[headers[c]] = (val) ? reinterpret_cast<const char*>(val) : "NULL";
        }
        result.push_back(row);
    }

    return result;
}

// Preparation statement with parameters
sqlite3_stmt* SQLiteDriver::prepareStatement(const std::string& query, const std::vector<std::string>& params) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        throw DatabaseError(std::string("SQLite prepare error: ") + err);
    }

    // Privyazyvaem parameters (in SQLite is used 1-based indexing)
    for (size_t i = 0; i < params.size(); ++i) {
        rc = sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_STATIC);
        if (rc != SQLITE_OK) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            throw DatabaseError(std::string("SQLite bind error: ") + err);
        }
    }

    return stmt;
}

DatabaseResponse SQLiteDriver::exec(const std::string& query, const std::vector<std::string>& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseResponse response;

    if (!connected_ || !db_) {
        response.http_status = boost::beast::http::status::internal_server_error;
        response.message = "Database not connected";
        return response;
    }

    try {
        sqlite3_stmt* stmt = nullptr;
        if (!params.empty()) {
            stmt = prepareStatement(query, params);
        } else {
            int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                std::string err = sqlite3_errmsg(db_);
                throw DatabaseError(std::string("SQLite prepare error: ") + err);
            }
        }

        std::string upperQuery = query;
        std::transform(upperQuery.begin(), upperQuery.end(), upperQuery.begin(), ::toupper);
        const bool isSelectQuery = (upperQuery.find("SELECT") != std::string::npos);
        const bool isInsertQuery = (upperQuery.find("INSERT") != std::string::npos);
        const bool hasResultColumns = sqlite3_column_count(stmt) > 0;

        if (hasResultColumns) {
            // SELECT and INSERT/UPDATE/DELETE ... RETURNING expose result columns.
            // Do not pre-step and re-prepare here: that executes RETURNING statements twice.
            response.data = parseResultSet(stmt);
            response.count = response.data.size();
            response.affected_rows = sqlite3_changes(db_);

            if (response.data.empty() && isSelectQuery) {
                response.http_status = boost::beast::http::status::not_found;
                response.message = "No data found for the given criteria";
            } else {
                response.http_status = isInsertQuery
                    ? boost::beast::http::status::created
                    : boost::beast::http::status::ok;
                response.message = isInsertQuery
                    ? "Record created successfully"
                    : "Query executed successfully";
            }
            response.response_has_header = true;
        } else if (int rc = sqlite3_step(stmt); rc == SQLITE_DONE) {
            // INSERT/UPDATE/DELETE request
            response.affected_rows = sqlite3_changes(db_);

            if (isInsertQuery) {
                // Emulation RETURNING through last_insert_rowid
                int64_t lastId = sqlite3_last_insert_rowid(db_);
                if (lastId > 0) {
                    std::string tableName = extractTableNameFromInsert(query);
                    if (!tableName.empty()) {
                        try {
                            response.data = fetchLastInsertedRow(tableName, lastId);
                            response.count = response.data.size();
                        } catch (...) {
                            // If not succeeded get data, keep only affected_rows
                        }
                    }
                }
            }

            if (response.affected_rows > 0 || !response.data.empty()) {
                response.http_status = boost::beast::http::status::ok;
                response.message = "Operation completed successfully";
            } else {
                response.http_status = boost::beast::http::status::not_found;
                response.message = "No records found matching the criteria";
            }
        } else {
            // Error execution
            std::string err = sqlite3_errmsg(db_);
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
        }

        sqlite3_finalize(stmt);

    } catch (const std::exception& e) {
        response.http_status = boost::beast::http::status::internal_server_error;
        response.message = "Internal server error: " + std::string(e.what());
    }

    return response;
}

void SQLiteDriver::connect(const std::string &connectionString) {
    std::lock_guard<std::mutex> lock(mutex_);
    connectionString_ = connectionString;
    filename_ = connectionString;

    int rc = sqlite3_open(filename_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "unknown";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        std::cout << "[SQLiteDriver] Connection failed: " << err << std::endl;
        throw ConnectionError(std::string("SQLite open error: ") + err);
    }
    connected_ = true;
    std::cout << "[SQLiteDriver] Connected to DB file: " << filename_ << std::endl;

    sqlite3_busy_timeout(db_, 5000);
    sqlite3_exec(db_,
                 "PRAGMA foreign_keys = ON; "
                 "PRAGMA journal_mode = WAL; "
                 "PRAGMA synchronous = NORMAL",
                 nullptr,
                 nullptr,
                 nullptr);
}

void SQLiteDriver::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;
    sqlite3_close(db_);
    db_ = nullptr;
    connected_ = false;
    std::cout << "[SQLiteDriver] Disconnected from DB file: " << filename_ << std::endl;
}

std::string SQLiteDriver::executeQueryWithHeaders(const std::string &query) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !db_) {
        throw ConnectionError("SQLite: not connected");
    }

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        throw DatabaseError(std::string("SQLite prepare error: ") + err);
    }

    std::ostringstream out;
    int cols = sqlite3_column_count(stmt);

    // Step 1: output names columns
    for (int c = 0; c < cols; ++c) {
        const char *name = sqlite3_column_name(stmt, c);
        if (name) out << name;
        else out << "UNKNOWN";
        if (c + 1 < cols) out << '\t';
    }

    bool hasRows = false;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!hasRows) {
            out << '\n';
            hasRows = true;
        } else {
            out << '\n';
        }

        for (int c = 0; c < cols; ++c) {
            const unsigned char *text = sqlite3_column_text(stmt, c);
            if (text) {
                out << reinterpret_cast<const char *>(text);
            } else {
                out << "NULL";
            }
            if (c + 1 < cols) out << '\t';
        }
    }

    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("SQLite step error: ") + err);
    }

    sqlite3_finalize(stmt);
    return out.str();
}

std::string SQLiteDriver::executeQueryWithParams(const std::string& query, const std::vector<std::string>& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !db_) {
        throw ConnectionError("SQLite: not connected");
    }

    sqlite3_stmt *stmt = prepareStatement(query, params);

    std::ostringstream out;
    int cols = sqlite3_column_count(stmt);

    // Step 1: output names columns
    for (int c = 0; c < cols; ++c) {
        const char *name = sqlite3_column_name(stmt, c);
        if (name) out << name;
        else out << "UNKNOWN";
        if (c + 1 < cols) out << '\t';
    }

    bool hasRows = false;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!hasRows) {
            out << '\n';
            hasRows = true;
        } else {
            out << '\n';
        }

        for (int c = 0; c < cols; ++c) {
            const unsigned char *text = sqlite3_column_text(stmt, c);
            if (text) {
                out << reinterpret_cast<const char *>(text);
            } else {
                out << "NULL";
            }
            if (c + 1 < cols) out << '\t';
        }
    }

    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("SQLite step error: ") + err);
    }

    sqlite3_finalize(stmt);
    return out.str();
}

std::string SQLiteDriver::executeQuery(const std::string &query, const std::string &dbSchema) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !db_) {
        throw ConnectionError("SQLite: not connected");
    }

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        throw DatabaseError(std::string("SQLite prepare error: ") + err);
    }

    std::ostringstream out;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int cols = sqlite3_column_count(stmt);
        for (int c = 0; c < cols; ++c) {
            const unsigned char *text = sqlite3_column_text(stmt, c);
            if (text) out << reinterpret_cast<const char *>(text);
            else out << "NULL";
            if (c + 1 < cols) out << '\t';
        }
        out << '\n';
    }

    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("SQLite step error: ") + err);
    }

    sqlite3_finalize(stmt);
    return out.str();
}

int SQLiteDriver::executeNonQuery(const std::string &query) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !db_) {
        throw ConnectionError("SQLite: not connected");
    }

    char *errmsg = nullptr;
    int rc = sqlite3_exec(db_, query.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "unknown";
        if (errmsg) sqlite3_free(errmsg);
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

    int changes = sqlite3_changes(db_);
    return changes;
}

int SQLiteDriver::executeUpdate(const std::string &query) {
    return executeNonQuery(query);
}

bool SQLiteDriver::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ && db_;
}

std::vector<std::string> SQLiteDriver::getTableNames() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !db_) {
        throw ConnectionError("Database not connected");
    }

    const std::string query =
        "SELECT name FROM sqlite_master "
        "WHERE type='table' AND name NOT LIKE 'sqlite_%' "
        "ORDER BY name";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        throw DatabaseError(std::string("SQLite prepare error: ") + err);
    }

    std::vector<std::string> table_names;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        if (name) {
            table_names.emplace_back(reinterpret_cast<const char*>(name));
        }
    }

    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("SQLite step error: ") + err);
    }

    sqlite3_finalize(stmt);
    return table_names;
}

std::vector<std::string> SQLiteDriver::getColumnNames(const std::string &table_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !db_) {
        throw ConnectionError("Database not connected");
    }

    std::string query = "PRAGMA table_info('" + table_name + "')";

    try {
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::string err = sqlite3_errmsg(db_);
            throw DatabaseError(std::string("SQLite prepare error: ") + err);
        }

        std::vector<std::string> column_names;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *name = sqlite3_column_text(stmt, 1); // Column name is at index 1
            if (name) {
                column_names.push_back(reinterpret_cast<const char*>(name));
            }
        }

        sqlite3_finalize(stmt);
        return column_names;
    } catch (const NotFoundError& e) {
        // If table not found, Return empty vector
        return {};
    }
}
