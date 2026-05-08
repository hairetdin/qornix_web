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
#include <libpq-fe.h>
#include <vector>

class PostgreSQLDriver : public IDatabase {
public:
    PostgreSQLDriver();

    virtual ~PostgreSQLDriver();

    void connect(const std::string &connectionString) override;
    void disconnect() override;
    DatabaseResponse exec(const std::string& query, const std::vector<std::string>& params = {}) override;
    std::string executeQuery(const std::string &query, const std::string &dbSchema = "public") override;
    std::string executeQueryWithHeaders(const std::string &query) override;

    std::string executeQueryWithParams(const std::string &query, const std::vector<std::string> &params);

    int executeNonQuery(const std::string &query) override;
    bool isConnected() const override;
    std::vector<std::string> getTableNames() override;
    std::vector<std::string> getColumnNames(const std::string &table_name) override;
    int executeUpdate(const std::string &query) override;

private:
    PGconn *conn_;
    bool connected_;
    std::string connectionString_;

    void createDatabase(const std::string &connectionString);

    // Helper Method for classification errors
    std::unique_ptr<DatabaseError> classifyDatabaseError(const std::string &errorMessage);

    // Helper Method for parsing results
    std::vector<std::map<std::string, std::string>> parseResultSet(PGresult* res);
};

