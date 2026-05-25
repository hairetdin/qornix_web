/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "auth_manager.h"
#include "handler_base.h"
#include "http_server.h"

#include <boost/json.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class AuthApiHandler : public HandlerBase {
private:
    std::shared_ptr<qornix_auth::AuthManager> authManager_;
    bool registrationEnabled_{false};
    bool secureCookies_{false};

    static std::string stringField(const boost::json::object& object, const char* key) {
        auto it = object.find(key);
        if (it == object.end() || !it->value().is_string()) {
            return "";
        }
        return std::string(it->value().as_string());
    }

    static std::string extractBearerToken(const http::request<http::string_body>& req) {
        auto authHeader = req.find("Authorization");
        if (authHeader == req.end()) {
            return "";
        }
        const std::string value(authHeader->value());
        constexpr const char* prefix = "Bearer ";
        if (value.rfind(prefix, 0) != 0) {
            return "";
        }
        return value.substr(std::char_traits<char>::length(prefix));
    }

    static std::string extractSessionId(const http::request<http::string_body>& req) {
        auto cookieHeader = req.find("Cookie");
        if (cookieHeader == req.end()) {
            return "";
        }
        const std::string cookies(cookieHeader->value());
        std::stringstream ss(cookies);
        std::string item;
        while (std::getline(ss, item, ';')) {
            item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char c) {
                return !std::isspace(c);
            }));
            item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char c) {
                return !std::isspace(c);
            }).base(), item.end());
            const auto eq = item.find('=');
            if (eq != std::string::npos && item.substr(0, eq) == "session_id") {
                return item.substr(eq + 1);
            }
        }
        return "";
    }

    static boost::json::array toJsonArray(const std::vector<std::string>& values) {
        boost::json::array result;
        for (const auto& value : values) {
            result.emplace_back(value);
        }
        return result;
    }

    static boost::json::object userObject(const qornix_auth::AuthResult& result) {
        boost::json::object user;
        user["id"] = result.userId;
        user["username"] = result.username;
        user["roles"] = toJsonArray(result.roles);
        user["permissions"] = toJsonArray(result.permissions);
        return user;
    }

    static boost::json::object userObject(const qornix_auth::User& user) {
        boost::json::object out;
        out["id"] = user.id;
        out["username"] = user.username;
        out["email"] = user.email;
        out["active"] = user.isActive;
        out["roles"] = toJsonArray(user.roles);
        out["permissions"] = toJsonArray(user.permissions);
        return out;
    }

    static boost::json::object contextObject(const qornix_auth::AuthContext& context) {
        boost::json::object user;
        user["id"] = context.userId;
        user["username"] = context.username;
        user["roles"] = toJsonArray(context.roles);
        user["permissions"] = toJsonArray(context.permissions);
        user["credential_type"] = context.credentialType;
        return user;
    }

    static std::vector<std::string> stringArrayField(const boost::json::object& object, const char* key) {
        std::vector<std::string> result;
        auto it = object.find(key);
        if (it == object.end()) {
            return result;
        }

        if (it->value().is_array()) {
            for (const auto& value : it->value().as_array()) {
                if (value.is_string()) {
                    result.emplace_back(value.as_string());
                }
            }
            return result;
        }

        if (it->value().is_string()) {
            std::stringstream ss(std::string(it->value().as_string()));
            std::string item;
            while (std::getline(ss, item, ',')) {
                item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char c) {
                    return !std::isspace(c);
                }));
                item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char c) {
                    return !std::isspace(c);
                }).base(), item.end());
                if (!item.empty()) {
                    result.push_back(item);
                }
            }
        }

        return result;
    }

    static std::optional<bool> boolField(const boost::json::object& object, const char* key) {
        auto it = object.find(key);
        if (it == object.end()) {
            return std::nullopt;
        }
        if (it->value().is_bool()) {
            return it->value().as_bool();
        }
        if (it->value().is_string()) {
            const std::string value(it->value().as_string());
            if (value == "true" || value == "1") return true;
            if (value == "false" || value == "0") return false;
        }
        return std::nullopt;
    }

    static bool hasField(const boost::json::object& object, const char* key) {
        return object.find(key) != object.end();
    }

    void writeAuthSuccess(Response& res, const qornix_auth::AuthResult& result) const {
        boost::json::object body;
        body["success"] = true;
        body["user"] = userObject(result);
        body["session_id"] = result.sessionId;
        body["token"] = result.token;

        res = make_json_response(http::status::ok, res.version(), boost::json::serialize(body));
        if (!result.sessionId.empty()) {
            std::string cookie = "session_id=" + result.sessionId + "; HttpOnly; SameSite=Lax; Path=/";
            if (secureCookies_) {
                cookie += "; Secure";
            }
            res.set(http::field::set_cookie, cookie);
        }
    }

    void handleLogin(const Request& req, Response& res) {
        boost::json::value parsed;
        try {
            parsed = boost::json::parse(req.body());
        } catch (const std::exception&) {
            buildErrorResponse(res, http::status::bad_request, "Invalid JSON body");
            return;
        }

        if (!parsed.is_object()) {
            buildErrorResponse(res, http::status::bad_request, "JSON object body required");
            return;
        }

        const auto& object = parsed.as_object();
        const std::string username = stringField(object, "username");
        const std::string password = stringField(object, "password");
        if (username.empty() || password.empty()) {
            buildErrorResponse(res, http::status::bad_request, "username and password are required");
            return;
        }

        auto result = authManager_->authenticate(username, password);
        if (result.status != qornix_auth::AuthStatus::SUCCESS) {
            buildErrorResponse(res, http::status::unauthorized, "Invalid credentials");
            return;
        }

        writeAuthSuccess(res, result);
    }

    void handleRegister(const Request& req, Response& res) {
        if (!registrationEnabled_) {
            buildErrorResponse(res, http::status::forbidden, "Registration is disabled");
            return;
        }

        boost::json::value parsed;
        try {
            parsed = boost::json::parse(req.body());
        } catch (const std::exception&) {
            buildErrorResponse(res, http::status::bad_request, "Invalid JSON body");
            return;
        }

        if (!parsed.is_object()) {
            buildErrorResponse(res, http::status::bad_request, "JSON object body required");
            return;
        }

        const auto& object = parsed.as_object();
        const std::string username = stringField(object, "username");
        const std::string password = stringField(object, "password");
        const std::string email = stringField(object, "email");
        auto result = authManager_->registerUser(username, password, email);
        if (result.status != qornix_auth::AuthStatus::SUCCESS) {
            buildErrorResponse(res, http::status::bad_request, result.message);
            return;
        }

        boost::json::object body;
        body["success"] = true;
        body["user"] = userObject(result);
        res = make_json_response(http::status::created, res.version(), boost::json::serialize(body));
    }

    void handleAdminListUsers(const Request& req, Response& res) {
        boost::json::array users;
        for (const auto& user : authManager_->store()->listUsers()) {
            users.emplace_back(userObject(user));
        }

        boost::json::object body;
        body["success"] = true;
        body["users"] = users;
        body["count"] = static_cast<int>(users.size());
        res = make_json_response(http::status::ok, req.version(), boost::json::serialize(body));
    }

    void handleAdminCreateUser(const Request& req, Response& res) {
        boost::json::value parsed;
        try {
            parsed = boost::json::parse(req.body());
        } catch (const std::exception&) {
            buildErrorResponse(res, http::status::bad_request, "Invalid JSON body");
            return;
        }

        if (!parsed.is_object()) {
            buildErrorResponse(res, http::status::bad_request, "JSON object body required");
            return;
        }

        const auto& object = parsed.as_object();
        const std::string username = stringField(object, "username");
        const std::string password = stringField(object, "password");
        const std::string email = stringField(object, "email");
        auto roles = stringArrayField(object, "roles");
        auto permissions = stringArrayField(object, "permissions");

        auto result = authManager_->registerUser(username, password, email, std::move(roles), std::move(permissions));
        if (result.status != qornix_auth::AuthStatus::SUCCESS) {
            buildErrorResponse(res, http::status::bad_request, result.message);
            return;
        }

        boost::json::object body;
        body["success"] = true;
        body["user"] = userObject(result);
        res = make_json_response(http::status::created, res.version(), boost::json::serialize(body));
    }

    void handleAdminUpdateUser(const Request& req, Response& res, const std::string& userId) {
        auto user = authManager_->store()->findUserById(userId);
        if (!user.has_value()) {
            buildErrorResponse(res, http::status::not_found, "User not found");
            return;
        }

        boost::json::value parsed;
        try {
            parsed = boost::json::parse(req.body());
        } catch (const std::exception&) {
            buildErrorResponse(res, http::status::bad_request, "Invalid JSON body");
            return;
        }

        if (!parsed.is_object()) {
            buildErrorResponse(res, http::status::bad_request, "JSON object body required");
            return;
        }

        const auto& object = parsed.as_object();
        if (hasField(object, "username")) {
            user->username = stringField(object, "username");
        }
        if (hasField(object, "email")) {
            user->email = stringField(object, "email");
        }
        if (hasField(object, "roles")) {
            user->roles = stringArrayField(object, "roles");
        }
        if (hasField(object, "permissions")) {
            user->permissions = stringArrayField(object, "permissions");
        }
        if (auto active = boolField(object, "active")) {
            user->isActive = *active;
        }

        if (hasField(object, "password")) {
            const std::string password = stringField(object, "password");
            if (!password.empty()) {
                auto hasher = qornix_auth::PasswordHasher::createDefault();
                std::string salt = hasher->generateSalt();
                user->salt = salt;
                user->password = hasher->hash(password, salt);
            }
        }

        if (!authManager_->store()->updateUser(*user)) {
            buildErrorResponse(res, http::status::bad_request, "User update failed");
            return;
        }

        boost::json::object body;
        body["success"] = true;
        body["user"] = userObject(*user);
        res = make_json_response(http::status::ok, res.version(), boost::json::serialize(body));
    }

    void handleMe(const Request& req, Response& res) {
        std::optional<qornix_auth::AuthContext> context;
        const std::string sessionId = extractSessionId(req);
        if (!sessionId.empty()) {
            context = authManager_->authenticateSession(sessionId);
        }

        if (!context.has_value()) {
            const std::string token = extractBearerToken(req);
            if (!token.empty()) {
                context = authManager_->authenticateBearerToken(token);
            }
        }

        if (!context.has_value()) {
            buildErrorResponse(res, http::status::unauthorized, "Authentication required");
            return;
        }

        boost::json::object body;
        body["success"] = true;
        body["user"] = contextObject(*context);
        res = make_json_response(http::status::ok, res.version(), boost::json::serialize(body));
    }

    void handleLogout(const Request& req, Response& res) {
        const std::string sessionId = extractSessionId(req);
        if (!sessionId.empty()) {
            authManager_->logout(sessionId);
        }

        boost::json::object body;
        body["success"] = true;
        res = make_json_response(http::status::ok, res.version(), boost::json::serialize(body));
        res.set(http::field::set_cookie, "session_id=; HttpOnly; SameSite=Lax; Path=/; Max-Age=0");
    }

protected:
    void handleGet(const Request& req, Response& res, const urls::url_view& url, const Params&) override {
        if (url.path() == "/auth/me") {
            handleMe(req, res);
            return;
        }
        if (url.path() == "/auth/users") {
            handleAdminListUsers(req, res);
            return;
        }
        buildErrorResponse(res, http::status::not_found, "Auth route not found");
    }

    void handlePost(const Request& req, Response& res, const urls::url_view& url, const Params&) override {
        if (url.path() == "/auth/login") {
            handleLogin(req, res);
            return;
        }
        if (url.path() == "/auth/logout") {
            handleLogout(req, res);
            return;
        }
        if (url.path() == "/auth/register") {
            handleRegister(req, res);
            return;
        }
        if (url.path() == "/auth/users") {
            handleAdminCreateUser(req, res);
            return;
        }
        buildErrorResponse(res, http::status::not_found, "Auth route not found");
    }

    void handlePatch(const Request& req, Response& res, const urls::url_view& url, const Params& pathParams) override {
        if (url.path().starts_with("/auth/users/")) {
            auto it = pathParams.find("id");
            std::string userId = it == pathParams.end()
                ? std::string(url.path().substr(std::string_view("/auth/users/").size()))
                : it->second;
            handleAdminUpdateUser(req, res, userId);
            return;
        }
        buildErrorResponse(res, http::status::not_found, "Auth route not found");
    }

public:
    explicit AuthApiHandler(std::shared_ptr<qornix_auth::AuthManager> authManager,
                            bool registrationEnabled = false,
                            bool secureCookies = false)
        : authManager_(std::move(authManager)),
          registrationEnabled_(registrationEnabled),
          secureCookies_(secureCookies) {}
};

inline std::shared_ptr<AuthApiHandler> makeAuthApiHandler(
    std::shared_ptr<qornix_auth::AuthManager> authManager,
    bool registrationEnabled = false,
    bool secureCookies = false) {
    return std::make_shared<AuthApiHandler>(std::move(authManager), registrationEnabled, secureCookies);
}

inline void setupAuthRoutes(HttpServer& server,
                            std::shared_ptr<qornix_auth::AuthManager> authManager,
                            bool registrationEnabled = false,
                            bool secureCookies = false) {
    auto handler = makeAuthApiHandler(std::move(authManager), registrationEnabled, secureCookies);
    server.add_route("/auth/login", handler);
    server.add_route("/auth/logout", handler);
    server.add_route("/auth/me", handler);
    server.add_route("/auth/register", handler);
    server.add_route("/auth/users", handler);
    server.add_route("/auth/users/{id}", handler);
}
