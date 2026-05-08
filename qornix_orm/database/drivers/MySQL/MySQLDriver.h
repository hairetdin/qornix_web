/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
// MySQLDriver.h
#pragma once

#include "../../IDatabase.h"
#include "../../exceptions.h"

#include <string>
#include <memory>
#include <mysql/mysql.h>

struct MYSQL;

class MySQLDriver : public IDatabase {
public:
    MySQLDriver();
   virtual ~MySQLDriver();

    void connect(const std::string& connectionString) override;
    void disconnect() override;
    DatabaseResponse exec(const std::string& query, const std::vector<std::string>& params = {}) override;
    std::string executeQuery(const std::string& query, const std::string& dbSchema = "public") override;
    std::string executeQueryWithHeaders(const std::string& query) override;
    std::string executeQueryWithParams(const std::string& query, const std::vector<std::string>& params) override;
    int executeNonQuery(const std::string& query) override;
    bool isConnected() const override;
    std::vector<std::string> getTableNames() override;
    std::vector<std::string> getColumnNames(const std::string& table_name) override;
    int executeUpdate(const std::string& query) override;

private:
    MYSQL* conn_;
    bool connected_;
    std::string connectionString_;
    std::string currentDatabase_;

    static void parseConnectionString(const std::string& cs, std::string& host, std::string& user,
                                      std::string& password, std::string& db, unsigned int& port,
                                      std::string& unix_socket);
    void createDatabaseIfNeeded(const std::string& host, const std::string& user, const std::string& password,
                                const std::string& db, unsigned int port, const std::string& unix_socket);
    std::string buildEscapedQueryWithParams(const std::string& query, const std::vector<std::string>& params);

    std::unique_ptr<DatabaseError> classifyDatabaseError(const std::string& errorMessage);
    std::vector<std::map<std::string, std::string>> parseResultSet(MYSQL_RES* res);
};
