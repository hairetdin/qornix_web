/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
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
    AsyncTableManager& filter(const std::map<std::string, std::string>& conditions);
    AsyncTableManager& limit(std::size_t value);
    AsyncTableManager& orderBy(std::string clause);
    AsyncTableManager& order_by(std::string clause) { return orderBy(std::move(clause)); }
    AsyncTableManager& select(std::vector<std::string> fields);
    AsyncTableManager& values(std::vector<std::string> fields) { return select(std::move(fields)); }
    AsyncTableManager& prepared(bool enabled = true);
    AsyncTableManager& timeout(std::chrono::milliseconds value);
    AsyncTableManager& queryOptions(qornix::db::QueryOptions options);

    boost::asio::awaitable<qornix::db::QueryResult> findAll(qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> all(qornix::db::CancellationToken token = {}) {
        co_return co_await findAll(std::move(token));
    }

    boost::asio::awaitable<qornix::db::QueryResult> findById(std::string id, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<std::optional<qornix::db::Row>> findOptionalById(std::string id, qornix::db::CancellationToken token = {});

    boost::asio::awaitable<qornix::db::QueryResult> insert(std::map<std::string, std::string> data, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> create(std::map<std::string, std::string> data, qornix::db::CancellationToken token = {}) {
        co_return co_await insert(std::move(data), std::move(token));
    }

    boost::asio::awaitable<qornix::db::QueryResult> update(std::string id, std::map<std::string, std::string> data, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> remove(std::string id, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> delete_(std::string id, qornix::db::CancellationToken token = {}) {
        co_return co_await remove(std::move(id), std::move(token));
    }

    boost::asio::awaitable<qornix::db::QueryResult> rawSql(std::string sql, qornix::db::QueryParams params = {}, qornix::db::CancellationToken token = {});
    boost::asio::awaitable<qornix::db::QueryResult> raw_sql(std::string sql, qornix::db::QueryParams params = {}, qornix::db::CancellationToken token = {}) {
        co_return co_await rawSql(std::move(sql), std::move(params), std::move(token));
    }

private:
    std::string buildSelectSql() const;
    std::string placeholder(std::size_t one_based_index) const;
    bool supportsReturning() const;
    qornix::db::QueryOptions queryOptionsCopy() const;
    static std::string joinFields(const std::vector<std::string>& fields);

    std::shared_ptr<AsyncDatabaseInterface> database_;
    std::string table_name_;
    std::map<std::string, std::string> filters_;
    std::vector<std::string> selected_fields_;
    std::string order_clause_;
    std::size_t limit_{0};
    qornix::db::QueryOptions query_options_{};
};
