/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "db/async_db_types.h"

class AsyncDatabaseInterface;

class AsyncTableManager {
public:
    AsyncTableManager(std::shared_ptr<AsyncDatabaseInterface> database, std::string table_name);

    AsyncTableManager& filter(const std::string& field, const std::string& value);
    AsyncTableManager& limit(std::size_t value);
    AsyncTableManager& orderBy(std::string clause);
    AsyncTableManager& select(std::vector<std::string> fields);

    boost::asio::awaitable<qornix::db::QueryResult> findAll(qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> findById(std::string id, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> insert(std::map<std::string, std::string> data, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> update(std::string id, std::map<std::string, std::string> data, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> remove(std::string id, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> rawSql(std::string sql, qornix::db::QueryParams params = {}, qornix::db::CancellationToken token = {});

private:
    std::string buildSelectSql() const;
    static std::string joinFields(const std::vector<std::string>& fields);

    std::shared_ptr<AsyncDatabaseInterface> database_;
    std::string table_name_;
    std::map<std::string, std::string> filters_;
    std::vector<std::string> selected_fields_;
    std::string order_clause_;
    std::size_t limit_{0};
};
