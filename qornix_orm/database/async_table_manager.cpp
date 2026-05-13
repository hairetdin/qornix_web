/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "async_table_manager.h"
#include "async_database_interface.h"

#include <algorithm>
#include <stdexcept>

AsyncTableManager::AsyncTableManager(std::shared_ptr<AsyncDatabaseInterface> database, std::string table_name)
    : database_(std::move(database)), table_name_(std::move(table_name)) {
    if (!database_) {
        throw std::invalid_argument("AsyncTableManager requires AsyncDatabaseInterface");
    }
}

AsyncTableManager& AsyncTableManager::filter(const std::string& field, const std::string& value) {
    filters_[field] = value;
    return *this;
}

AsyncTableManager& AsyncTableManager::limit(std::size_t value) {
    limit_ = value;
    return *this;
}

AsyncTableManager& AsyncTableManager::orderBy(std::string clause) {
    order_clause_ = std::move(clause);
    return *this;
}

AsyncTableManager& AsyncTableManager::select(std::vector<std::string> fields) {
    selected_fields_ = std::move(fields);
    return *this;
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncTableManager::findAll(qornix::db::CancellationToken token) {
    qornix::db::QueryParams params;
    for (const auto& [_, value] : filters_) {
        params.push_back(qornix::db::QueryParam::text(value));
    }
    const std::string sql = buildSelectSql();
    auto result = co_await database_->query(sql, std::move(params), {}, std::move(token));
    co_return result;
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncTableManager::findById(std::string id, qornix::db::CancellationToken token) {
    const std::string sql = "SELECT * FROM " + table_name_ + " WHERE id = $1 LIMIT 1";
    qornix::db::QueryParams params;
    params.push_back(qornix::db::QueryParam::text(std::move(id)));
    auto result = co_await database_->queryOne(sql, std::move(params), {}, std::move(token));
    co_return result;
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncTableManager::insert(
    std::map<std::string, std::string> data,
    qornix::db::CancellationToken token) {
    if (data.empty()) {
        throw std::invalid_argument("insert data cannot be empty");
    }
    std::ostringstream columns;
    std::ostringstream placeholders;
    qornix::db::QueryParams params;
    std::size_t index = 1;
    for (const auto& [field, value] : data) {
        if (index > 1) {
            columns << ", ";
            placeholders << ", ";
        }
        columns << field;
        placeholders << "$" << index++;
        params.push_back(qornix::db::QueryParam::text(value));
    }
    const std::string sql = "INSERT INTO " + table_name_ + " (" + columns.str() + ") VALUES (" + placeholders.str() + ") RETURNING *";
    auto result = co_await database_->query(sql, std::move(params), {}, std::move(token));
    co_return result;
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncTableManager::update(
    std::string id,
    std::map<std::string, std::string> data,
    qornix::db::CancellationToken token) {
    if (data.empty()) {
        throw std::invalid_argument("update data cannot be empty");
    }
    std::ostringstream assignments;
    qornix::db::QueryParams params;
    std::size_t index = 1;
    for (const auto& [field, value] : data) {
        if (index > 1) {
            assignments << ", ";
        }
        assignments << field << " = $" << index++;
        params.push_back(qornix::db::QueryParam::text(value));
    }
    params.push_back(qornix::db::QueryParam::text(std::move(id)));
    const std::string sql = "UPDATE " + table_name_ + " SET " + assignments.str() + " WHERE id = $" + std::to_string(index) + " RETURNING *";
    auto result = co_await database_->query(sql, std::move(params), {}, std::move(token));
    co_return result;
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncTableManager::remove(std::string id, qornix::db::CancellationToken token) {
    const std::string sql = "DELETE FROM " + table_name_ + " WHERE id = $1";
    qornix::db::QueryParams params;
    params.push_back(qornix::db::QueryParam::text(std::move(id)));
    auto result = co_await database_->execute(sql, std::move(params), {}, std::move(token));
    co_return result;
}

boost::asio::awaitable<qornix::db::QueryResult> AsyncTableManager::rawSql(
    std::string sql,
    qornix::db::QueryParams params,
    qornix::db::CancellationToken token) {
    auto result = co_await database_->query(std::move(sql), std::move(params), {}, std::move(token));
    co_return result;
}

std::string AsyncTableManager::buildSelectSql() const {
    std::ostringstream sql;
    sql << "SELECT " << (selected_fields_.empty() ? "*" : joinFields(selected_fields_)) << " FROM " << table_name_;
    std::size_t index = 1;
    for (const auto& [field, _] : filters_) {
        sql << (index == 1 ? " WHERE " : " AND ") << field << " = $" << index;
        ++index;
    }
    if (!order_clause_.empty()) {
        sql << " ORDER BY " << order_clause_;
    }
    if (limit_ > 0) {
        sql << " LIMIT " << limit_;
    }
    return sql.str();
}

std::string AsyncTableManager::joinFields(const std::vector<std::string>& fields) {
    std::ostringstream out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << fields[i];
    }
    return out.str();
}
