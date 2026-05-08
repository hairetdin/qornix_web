/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "handler_base.h"
#include "query_builder.h"
#include "database_interface.h"
#include "database/config.h"
#include "schema_loader.h"
#include "auth_manager.h"
#include "auth_middleware.h"

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wiki_auth {
namespace json = boost::json;

inline std::string nowTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

inline std::string trim(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

inline std::string getJsonString(const json::object& object,
                                 const std::string& key,
                                 const std::string& fallback = "") {
    const auto it = object.find(key);
    if (it == object.end() || it->value().is_null() || !it->value().is_string()) {
        return fallback;
    }
    return std::string(it->value().as_string().c_str());
}

inline std::string objectString(const json::object& object,
                                const std::string& key,
                                const std::string& fallback = "") {
    const auto it = object.find(key);
    if (it == object.end() || it->value().is_null()) {
        return fallback;
    }
    if (it->value().is_string()) return std::string(it->value().as_string().c_str());
    if (it->value().is_int64()) return std::to_string(it->value().as_int64());
    if (it->value().is_uint64()) return std::to_string(it->value().as_uint64());
    if (it->value().is_double()) return std::to_string(it->value().as_double());
    if (it->value().is_bool()) return it->value().as_bool() ? "1" : "0";
    return fallback;
}

// ---------------------------------------------------------------------------
// RegisterHandler — POST /api/auth/register
// ---------------------------------------------------------------------------

class RegisterHandler : public HandlerBase {
private:
    std::shared_ptr<qornix_auth::AuthManager> authManager_;
    std::shared_ptr<DatabaseInterface> db_;

    void ensureUsersTable() {
        if (!db_ || !db_->isConnected()) return;

        // Load schema and create users table if it doesn't exist
        if (SchemaLoader::getEntities().empty()) {
            // Schema not loaded yet, try to load from wiki_schema.xml
            std::filesystem::path schemaPath = std::filesystem::current_path() / "wiki_schema.xml";
            if (!SchemaLoader::loadSchemaFromFile(schemaPath.string())) {
                return; // Schema loading failed, skip table creation
            }
        }

        auto it = SchemaLoader::getEntities().find("Users");
        if (it == SchemaLoader::getEntities().end()) return;

        const auto& entity = it->second;

        // Check if table exists
        auto tables = db_->getTableNames();
        bool exists = std::find(tables.begin(), tables.end(), entity.tableName) != tables.end();
        if (exists) return;

        // Create table using SchemaLoader
        SchemaLoader::createDBTable(db_, entity);
    }

    void persistUserToDb(const qornix_auth::User& user) {
        if (!db_ || !db_->isConnected()) return;

        QueryBuilder builder;
        builder.setDatabaseInterface(db_);
        builder.setMethod("POST").setTable("users");

        json::object data;
        data["username"] = user.username;
        data["email"] = user.email;
        data["password_hash"] = user.password;
        data["salt"] = user.salt;
        data["is_active"] = user.isActive ? 1 : 0;
        data["created_at"] = nowTimestamp();

        const auto response = builder.exec();
        if (response.status != http::status::created && response.status != http::status::ok) {
            // Log error but don't fail the registration
        }
    }

public:
    RegisterHandler(std::shared_ptr<qornix_auth::AuthManager> authManager,
                    std::shared_ptr<DatabaseInterface> db)
        : authManager_(authManager), db_(db) {
    }

    void handlePost(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        // Parse request body
        std::string body(req.body());
        json::value parsed;
        try {
            parsed = json::parse(body);
        } catch (const std::exception& e) {
            buildErrorResponse(res, http::status::bad_request, "Invalid JSON");
            return;
        }

        if (!parsed.is_object()) {
            buildErrorResponse(res, http::status::bad_request, "Request body must be a JSON object");
            return;
        }

        const auto& object = parsed.as_object();
        std::string username = trim(getJsonString(object, "username"));
        std::string password = getJsonString(object, "password");
        std::string email = trim(getJsonString(object, "email", ""));

        // Validate input
        if (username.empty()) {
            buildErrorResponse(res, http::status::bad_request, "Username is required");
            return;
        }
        if (password.empty()) {
            buildErrorResponse(res, http::status::bad_request, "Password is required");
            return;
        }
        if (email.empty()) {
            buildErrorResponse(res, http::status::bad_request, "Email is required");
            return;
        }

        // Register user
        auto result = authManager_->registerUser(username, password, email);

        if (result.status != qornix_auth::AuthStatus::SUCCESS) {
            buildErrorResponse(res, http::status::bad_request,
                               result.status == qornix_auth::AuthStatus::USER_ALREADY_EXISTS
                                   ? "Username or email already exists"
                                   : "Registration failed");
            return;
        }

        // Ensure users table exists, then persist to database
        ensureUsersTable();
        const auto* user = authManager_->getUserById(result.userId);
        if (user) {
            persistUserToDb(*user);
        }

        // Return success response
        json::object response;
        response["message"] = "User registered successfully";
        response["userId"] = result.userId;
        response["username"] = username;
        response["email"] = email;

        buildJsonResponse(res, http::status::created, json::serialize(response));
    }
};

// ---------------------------------------------------------------------------
// LoginHandler — POST /api/auth/login
// ---------------------------------------------------------------------------

class LoginHandler : public HandlerBase {
private:
    std::shared_ptr<qornix_auth::AuthManager> authManager_;
    std::shared_ptr<DatabaseInterface> db_;

    void updateLastLoginInDb(const std::string& userId) {
        if (!db_ || !db_->isConnected()) return;

        QueryBuilder builder;
        builder.setDatabaseInterface(db_);
        builder.setMethod("PATCH").setTable("users");
        builder.addFilter("id=" + userId);

        json::object data;
        data["last_login_at"] = nowTimestamp();

        const auto response = builder.exec();
        if (response.status != http::status::ok) {
            // Log error but don't fail login
        }
    }

public:
    LoginHandler(std::shared_ptr<qornix_auth::AuthManager> authManager,
                 std::shared_ptr<DatabaseInterface> db)
        : authManager_(authManager), db_(db) {
    }

    void handlePost(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        // Parse request body
        std::string body(req.body());
        json::value parsed;
        try {
            parsed = json::parse(body);
        } catch (const std::exception& e) {
            buildErrorResponse(res, http::status::bad_request, "Invalid JSON");
            return;
        }

        if (!parsed.is_object()) {
            buildErrorResponse(res, http::status::bad_request, "Request body must be a JSON object");
            return;
        }

        const auto& object = parsed.as_object();
        std::string username = trim(getJsonString(object, "username"));
        std::string password = getJsonString(object, "password");

        if (username.empty()) {
            buildErrorResponse(res, http::status::bad_request, "Username is required");
            return;
        }
        if (password.empty()) {
            buildErrorResponse(res, http::status::bad_request, "Password is required");
            return;
        }

        // Authenticate
        auto result = authManager_->authenticate(username, password);

        if (result.status != qornix_auth::AuthStatus::SUCCESS) {
            buildErrorResponse(res, http::status::unauthorized, "Invalid username or password");
            return;
        }

        // Update last_login_at in DB
        updateLastLoginInDb(result.userId);

        // Parse result message to extract session and token info
        // Format: "Authentication successful|session:<id>|token:<jwt>"
        std::string sessionId;
        std::string jwtToken;

        std::istringstream stream(result.message);
        std::string part;
        while (std::getline(stream, part, '|')) {
            const auto trimmed = trim(part);
            if (trimmed.find("session:") == 0) {
                sessionId = trimmed.substr(8);
            } else if (trimmed.find("token:") == 0) {
                jwtToken = trimmed.substr(6);
            }
        }

        // Get user info
        const auto* user = authManager_->getUserById(result.userId);

        // Build response
        json::object response;
        response["message"] = "Login successful";
        response["userId"] = result.userId;
        response["username"] = user ? user->username : username;
        response["email"] = user ? user->email : "";
        response["sessionId"] = sessionId;
        response["jwtToken"] = jwtToken;

        // Set session cookie if session-based auth is enabled
        if (!sessionId.empty()) {
            res.set(http::field::set_cookie,
                    "session_id=" + sessionId + "; Path=/; HttpOnly; SameSite=Lax");
        }

        buildJsonResponse(res, http::status::ok, json::serialize(response));
    }
};

// ---------------------------------------------------------------------------
// MeHandler — GET /api/auth/me
// Returns current authenticated user info
// ---------------------------------------------------------------------------

class MeHandler : public HandlerBase {
private:
    std::shared_ptr<qornix_auth::AuthManager> authManager_;

    std::string extractSessionId(const http::request<http::string_body>& req) const {
        auto cookieHeader = req.find("Cookie");
        if (cookieHeader != req.end()) {
            std::string cookieStr(cookieHeader->value());
            size_t pos = cookieStr.find("session_id=");
            if (pos != std::string::npos) {
                size_t start = pos + 11;
                size_t end = cookieStr.find(';', start);
                if (end == std::string::npos) end = cookieStr.length();
                return cookieStr.substr(start, end - start);
            }
        }
        return "";
    }

    std::string extractBearerToken(const http::request<http::string_body>& req) const {
        auto authHeader = req.find("Authorization");
        if (authHeader != req.end()) {
            std::string authValue(authHeader->value());
            if (authValue.substr(0, 7) == "Bearer ") {
                return authValue.substr(7);
            }
        }
        return "";
    }

public:
    explicit MeHandler(std::shared_ptr<qornix_auth::AuthManager> authManager)
        : authManager_(authManager) {}

    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        std::string sessionId = extractSessionId(req);
        std::string bearerToken = extractBearerToken(req);

        std::string userId;
        std::string username;

        // Try session first
        if (!sessionId.empty()) {
            auto session = authManager_->validateSession(sessionId);
            if (session && !session->isExpired()) {
                userId = session->userId;
                username = session->username;
            }
        }

        // If no session, try Bearer token
        if (userId.empty() && !bearerToken.empty()) {
            auto tokenResult = authManager_->validateJwtToken(bearerToken);
            if (tokenResult.success && tokenResult.claims) {
                userId = tokenResult.claims->subject;
                auto it = tokenResult.claims->customClaims.find("username");
                if (it != tokenResult.claims->customClaims.end()) {
                    username = it->second;
                }
            }
        }

        if (userId.empty()) {
            buildErrorResponse(res, http::status::unauthorized, "Not authenticated");
            return;
        }

        const auto* user = authManager_->getUserById(userId);
        if (!user) {
            buildErrorResponse(res, http::status::unauthorized, "User not found");
            return;
        }

        json::object response;
        response["id"] = user->id;
        response["username"] = user->username;
        response["email"] = user->email;
        response["isActive"] = user->isActive;

        buildJsonResponse(res, http::status::ok, json::serialize(response));
    }
};

} // namespace wiki_auth
