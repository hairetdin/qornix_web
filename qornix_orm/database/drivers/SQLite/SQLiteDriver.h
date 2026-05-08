/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include "../../IDatabase.h"
#include "../../exceptions.h"

#include <string>
#include <memory>
#include <mutex>

struct sqlite3;
struct sqlite3_stmt;


class SQLiteDriver : public IDatabase {
public:
    SQLiteDriver();
    virtual ~SQLiteDriver();

    void connect(const std::string &connectionString) override;
    void disconnect() override;
    DatabaseResponse exec(const std::string& query, const std::vector<std::string>& params = {}) override;
    std::string executeQuery(const std::string &query, const std::string &dbSchema = "public") override;
    std::string executeQueryWithHeaders(const std::string &query) override;
    std::string executeQueryWithParams(const std::string &query, const std::vector<std::string> &params) override;
    int executeNonQuery(const std::string &query) override;
    bool isConnected() const override;
    std::vector<std::string> getTableNames() override;
    std::vector<std::string> getColumnNames(const std::string &table_name) override;
    int executeUpdate(const std::string &query) override;

private:
    sqlite3* db_;
    bool connected_;
    std::string filename_;
    std::string connectionString_;
    mutable std::mutex mutex_;

    // Check support RETURNING (SQLite 3.35+)
    bool supportsReturning() const;

    // Extract name table from INSERT request
    std::string extractTableNameFromInsert(const std::string& query) const;

    // Get last vstavlennoy record
    std::vector<std::map<std::string, std::string>> fetchLastInsertedRow(
        const std::string& tableName,
        int64_t lastId) const;

    // Helper Method for classification errors
    std::unique_ptr<DatabaseError> classifyDatabaseError(const std::string &errorMessage);

    // Helper Method for parsing results
    std::vector<std::map<std::string, std::string>> parseResultSet(sqlite3_stmt* stmt) const;

    // Preparation statement with parameters
    sqlite3_stmt* prepareStatement(const std::string& query, const std::vector<std::string>& params);
};
