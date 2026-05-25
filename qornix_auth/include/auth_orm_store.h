/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "auth_store.h"

#include "database_interface.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace qornix_auth {

class QornixOrmAuthStore : public AuthStore {
private:
    std::shared_ptr<DatabaseInterface> db_;
    std::string driver_;

    std::string placeholder(std::size_t index) const {
        if (driver_ == "sqlite" || driver_ == "sqlite3") {
            return "?";
        }
        return "$" + std::to_string(index);
    }

    std::string insertIgnorePrefix() const {
        if (driver_ == "mysql") {
            return "INSERT IGNORE";
        }
        return "INSERT";
    }

    std::string insertIgnoreSuffix() const {
        if (driver_ == "postgres" || driver_ == "postgresql" ||
            driver_ == "sqlite" || driver_ == "sqlite3") {
            return " ON CONFLICT DO NOTHING";
        }
        return "";
    }

    static std::int64_t toMillis(std::chrono::system_clock::time_point value) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
    }

    static std::chrono::system_clock::time_point fromMillis(const std::string& value) {
        if (value.empty() || value == "NULL") {
            return std::chrono::system_clock::now();
        }
        return std::chrono::system_clock::time_point{std::chrono::milliseconds{std::stoll(value)}};
    }

    static bool boolFromDb(const std::string& value) {
        return value == "1" || value == "true" || value == "TRUE" || value == "t";
    }

    void execIgnoringNoRows(const std::string& sql, const std::vector<std::string>& params = {}) const {
        auto response = db_->exec(sql, params);
        const auto code = static_cast<unsigned>(response.http_status);
        if (code >= 500) {
            throw std::runtime_error("Auth store database error: " + response.message);
        }
    }

    std::vector<std::string> loadValues(const std::string& sql,
                                        const std::vector<std::string>& params,
                                        const std::string& column) const {
        auto response = db_->exec(sql, params);
        const auto code = static_cast<unsigned>(response.http_status);
        if (code >= 500) {
            throw std::runtime_error("Auth store database error: " + response.message);
        }

        std::vector<std::string> result;
        for (const auto& row : response.data) {
            auto it = row.find(column);
            if (it != row.end() && it->second != "NULL") {
                result.push_back(it->second);
            }
        }
        return result;
    }

    std::vector<std::string> loadRoles(const std::string& userId) const {
        return loadValues(
            "SELECT role FROM auth_user_roles WHERE user_id = " + placeholder(1) + " ORDER BY role",
            {userId},
            "role");
    }

    std::vector<std::string> loadPermissions(const std::string& userId) const {
        return loadValues(
            "SELECT permission FROM auth_user_permissions WHERE user_id = " + placeholder(1) + " ORDER BY permission",
            {userId},
            "permission");
    }

    void replaceRoles(const User& user) const {
        execIgnoringNoRows("DELETE FROM auth_user_roles WHERE user_id = " + placeholder(1), {user.id});
        for (const auto& role : user.roles) {
            if (role.empty()) {
                continue;
            }
            execIgnoringNoRows(
                insertIgnorePrefix() + " INTO auth_roles (role) VALUES (" + placeholder(1) + ")" + insertIgnoreSuffix(),
                {role});
            execIgnoringNoRows(
                insertIgnorePrefix() + " INTO auth_user_roles (user_id, role) VALUES (" +
                    placeholder(1) + ", " + placeholder(2) + ")" + insertIgnoreSuffix(),
                {user.id, role});
        }
    }

    void replacePermissions(const User& user) const {
        execIgnoringNoRows("DELETE FROM auth_user_permissions WHERE user_id = " + placeholder(1), {user.id});
        for (const auto& permission : user.permissions) {
            if (permission.empty()) {
                continue;
            }
            execIgnoringNoRows(
                insertIgnorePrefix() + " INTO auth_permissions (permission) VALUES (" +
                    placeholder(1) + ")" + insertIgnoreSuffix(),
                {permission});
            execIgnoringNoRows(
                insertIgnorePrefix() + " INTO auth_user_permissions (user_id, permission) VALUES (" +
                    placeholder(1) + ", " + placeholder(2) + ")" + insertIgnoreSuffix(),
                {user.id, permission});
        }
    }

    std::optional<User> rowToUser(const std::map<std::string, std::string>& row) const {
        auto id = row.find("id");
        auto username = row.find("username");
        if (id == row.end() || username == row.end()) {
            return std::nullopt;
        }

        User user;
        user.id = id->second;
        user.username = username->second;
        if (auto it = row.find("email"); it != row.end() && it->second != "NULL") {
            user.email = it->second;
        }
        if (auto it = row.find("password_hash"); it != row.end() && it->second != "NULL") {
            user.password = it->second;
        }
        if (auto it = row.find("salt"); it != row.end() && it->second != "NULL") {
            user.salt = it->second;
        }
        if (auto it = row.find("is_active"); it != row.end()) {
            user.isActive = boolFromDb(it->second);
        }
        if (auto it = row.find("created_at"); it != row.end()) {
            user.createdAt = fromMillis(it->second);
        }
        if (auto it = row.find("last_login_at"); it != row.end()) {
            user.lastLoginAt = fromMillis(it->second);
        }

        user.roles = loadRoles(user.id);
        user.permissions = loadPermissions(user.id);
        return user;
    }

public:
    explicit QornixOrmAuthStore(std::shared_ptr<DatabaseInterface> db)
        : db_(std::move(db)) {
        if (!db_) {
            throw std::invalid_argument("QornixOrmAuthStore requires a DatabaseInterface");
        }
        driver_ = db_->getDatabaseConfig().driver;
        if (driver_.empty()) {
            driver_ = "sqlite";
        }
    }

    static std::shared_ptr<QornixOrmAuthStore> create(std::shared_ptr<DatabaseInterface> db,
                                                       bool migrate = true) {
        auto store = std::make_shared<QornixOrmAuthStore>(std::move(db));
        if (migrate) {
            store->migrate();
        }
        return store;
    }

    void migrate() const {
        execIgnoringNoRows(
            "CREATE TABLE IF NOT EXISTS auth_users ("
            "id VARCHAR(128) PRIMARY KEY, "
            "username VARCHAR(255) NOT NULL UNIQUE, "
            "email VARCHAR(512), "
            "password_hash TEXT NOT NULL, "
            "salt VARCHAR(255) NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, "
            "created_at BIGINT NOT NULL, "
            "last_login_at BIGINT NOT NULL"
            ")");

        execIgnoringNoRows(
            "CREATE TABLE IF NOT EXISTS auth_roles ("
            "role VARCHAR(128) PRIMARY KEY"
            ")");

        execIgnoringNoRows(
            "CREATE TABLE IF NOT EXISTS auth_permissions ("
            "permission VARCHAR(255) PRIMARY KEY"
            ")");

        execIgnoringNoRows(
            "CREATE TABLE IF NOT EXISTS auth_user_roles ("
            "user_id VARCHAR(128) NOT NULL, "
            "role VARCHAR(128) NOT NULL, "
            "PRIMARY KEY (user_id, role)"
            ")");

        execIgnoringNoRows(
            "CREATE TABLE IF NOT EXISTS auth_user_permissions ("
            "user_id VARCHAR(128) NOT NULL, "
            "permission VARCHAR(255) NOT NULL, "
            "PRIMARY KEY (user_id, permission)"
            ")");
    }

    bool createUser(const User& user) override {
        auto existing = findUserByUsername(user.username);
        if (existing.has_value()) {
            return false;
        }

        auto response = db_->exec(
            "INSERT INTO auth_users "
            "(id, username, email, password_hash, salt, is_active, created_at, last_login_at) "
            "VALUES (" + placeholder(1) + ", " + placeholder(2) + ", " + placeholder(3) + ", " +
                placeholder(4) + ", " + placeholder(5) + ", " + placeholder(6) + ", " +
                placeholder(7) + ", " + placeholder(8) + ")",
            {
                user.id,
                user.username,
                user.email,
                user.password,
                user.salt,
                user.isActive ? "1" : "0",
                std::to_string(toMillis(user.createdAt)),
                std::to_string(toMillis(user.lastLoginAt)),
            });
        const auto code = static_cast<unsigned>(response.http_status);
        if (code >= 400) {
            return false;
        }

        replaceRoles(user);
        replacePermissions(user);
        return true;
    }

    bool updateUser(const User& user) override {
        if (!findUserById(user.id).has_value()) {
            return false;
        }

        auto response = db_->exec(
            "UPDATE auth_users SET "
            "username = " + placeholder(1) + ", "
            "email = " + placeholder(2) + ", "
            "password_hash = " + placeholder(3) + ", "
            "salt = " + placeholder(4) + ", "
            "is_active = " + placeholder(5) + ", "
            "created_at = " + placeholder(6) + ", "
            "last_login_at = " + placeholder(7) + " "
            "WHERE id = " + placeholder(8),
            {
                user.username,
                user.email,
                user.password,
                user.salt,
                user.isActive ? "1" : "0",
                std::to_string(toMillis(user.createdAt)),
                std::to_string(toMillis(user.lastLoginAt)),
                user.id,
            });
        const auto code = static_cast<unsigned>(response.http_status);
        if (code >= 500) {
            return false;
        }

        replaceRoles(user);
        replacePermissions(user);
        return true;
    }

    std::optional<User> findUserById(const std::string& userId) const override {
        auto response = db_->exec(
            "SELECT id, username, email, password_hash, salt, is_active, created_at, last_login_at "
            "FROM auth_users WHERE id = " + placeholder(1) + " LIMIT 1",
            {userId});
        if (response.data.empty()) {
            return std::nullopt;
        }
        return rowToUser(response.data.front());
    }

    std::optional<User> findUserByUsername(const std::string& username) const override {
        auto response = db_->exec(
            "SELECT id, username, email, password_hash, salt, is_active, created_at, last_login_at "
            "FROM auth_users WHERE username = " + placeholder(1) + " LIMIT 1",
            {username});
        if (response.data.empty()) {
            return std::nullopt;
        }
        return rowToUser(response.data.front());
    }

    std::vector<User> listUsers() const override {
        auto response = db_->exec(
            "SELECT id, username, email, password_hash, salt, is_active, created_at, last_login_at "
            "FROM auth_users ORDER BY username");
        std::vector<User> result;
        result.reserve(response.data.size());
        for (const auto& row : response.data) {
            auto user = rowToUser(row);
            if (user.has_value()) {
                result.push_back(*user);
            }
        }
        return result;
    }
};

} // namespace qornix_auth
