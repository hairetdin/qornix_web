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
#include <chrono>
#include <cstdint>
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
    std::string sameSite_{"Lax"};
    std::string cookiePath_{"/"};

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

    static boost::json::object auditObject(const qornix_auth::AuthAuditEvent& event) {
        boost::json::object out;
        out["type"] = event.type;
        out["user_id"] = event.userId;
        out["username"] = event.username;
        out["outcome"] = event.outcome;
        out["detail"] = event.detail;
        out["at_ms"] = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                event.at.time_since_epoch()).count());
        return out;
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

    std::string csrfTokenForSession(const std::string& sessionId) const {
        return sessionId.empty() ? "" : authManager_->csrfTokenForSession(sessionId);
    }

    std::string sessionCookie(const std::string& sessionId, bool clear = false) const {
        std::string cookie = "session_id=" + sessionId + "; HttpOnly; SameSite=" + sameSite_ + "; Path=" + cookiePath_;
        if (secureCookies_) {
            cookie += "; Secure";
        }
        if (clear) {
            cookie += "; Max-Age=0";
        }
        return cookie;
    }

    void writeAuthSuccess(Response& res, const qornix_auth::AuthResult& result) const {
        boost::json::object body;
        body["success"] = true;
        body["user"] = userObject(result);
        body["session_id"] = result.sessionId;
        body["token"] = result.token;
        const std::string csrfToken = csrfTokenForSession(result.sessionId);
        if (!csrfToken.empty()) {
            body["csrf_token"] = csrfToken;
        }

        res = make_json_response(http::status::ok, res.version(), boost::json::serialize(body));
        if (!result.sessionId.empty()) {
            res.set(http::field::set_cookie, sessionCookie(result.sessionId));
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
        authManager_->invalidateSessionsForUser(user->id);

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
        if (context->credentialType == "session") {
            const std::string csrfToken = csrfTokenForSession(context->sessionId);
            if (!csrfToken.empty()) {
                body["csrf_token"] = csrfToken;
            }
        }
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
        res.set(http::field::set_cookie, sessionCookie("", true));
    }

    void handleAdminCreateInvite(const Request& req, Response& res) {
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
        if (username.empty()) {
            buildErrorResponse(res, http::status::bad_request, "username is required");
            return;
        }
        std::chrono::minutes ttl(1440);
        auto ttlIt = object.find("ttl_minutes");
        if (ttlIt != object.end() && ttlIt->value().is_int64()) {
            ttl = std::chrono::minutes(std::max<std::int64_t>(1, ttlIt->value().as_int64()));
        }
        const std::string token = authManager_->createInviteToken(
            username,
            stringField(object, "email"),
            stringArrayField(object, "roles"),
            stringArrayField(object, "permissions"),
            ttl);
        boost::json::object body;
        body["success"] = true;
        body["invite_token"] = token;
        body["delivery"] = "manual";
        res = make_json_response(http::status::created, res.version(), boost::json::serialize(body));
    }

    void handleAcceptInvite(const Request& req, Response& res) {
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
        auto result = authManager_->acceptInviteToken(
            stringField(object, "token"),
            stringField(object, "password"),
            stringField(object, "username"),
            stringField(object, "email"));
        if (result.status != qornix_auth::AuthStatus::SUCCESS) {
            buildErrorResponse(res, http::status::bad_request, result.message);
            return;
        }
        boost::json::object body;
        body["success"] = true;
        body["user"] = userObject(result);
        res = make_json_response(http::status::created, res.version(), boost::json::serialize(body));
    }

    void handlePasswordResetRequest(const Request& req, Response& res) {
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
        const std::string token = authManager_->createPasswordResetToken(stringField(object, "username"));
        boost::json::object body;
        body["success"] = true;
        body["delivery"] = "manual";
        if (!token.empty()) {
            body["reset_token"] = token;
        }
        res = make_json_response(http::status::ok, res.version(), boost::json::serialize(body));
    }

    void handlePasswordResetConfirm(const Request& req, Response& res) {
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
        if (!authManager_->resetPasswordWithToken(stringField(object, "token"), stringField(object, "password"))) {
            buildErrorResponse(res, http::status::bad_request, "Invalid token or password");
            return;
        }
        boost::json::object body;
        body["success"] = true;
        res = make_json_response(http::status::ok, res.version(), boost::json::serialize(body));
    }

    void handleAdminAuditEvents(const Request& req, Response& res) {
        boost::json::array events;
        for (const auto& event : authManager_->auditEvents()) {
            events.emplace_back(auditObject(event));
        }
        boost::json::object body;
        body["success"] = true;
        body["events"] = events;
        body["count"] = static_cast<int>(events.size());
        res = make_json_response(http::status::ok, req.version(), boost::json::serialize(body));
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
        if (url.path() == "/auth/audit") {
            handleAdminAuditEvents(req, res);
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
        if (url.path() == "/auth/invites") {
            handleAdminCreateInvite(req, res);
            return;
        }
        if (url.path() == "/auth/invites/accept") {
            handleAcceptInvite(req, res);
            return;
        }
        if (url.path() == "/auth/password-reset/request") {
            handlePasswordResetRequest(req, res);
            return;
        }
        if (url.path() == "/auth/password-reset/confirm") {
            handlePasswordResetConfirm(req, res);
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
                            bool secureCookies = false,
                            std::string sameSite = "Lax",
                            std::string cookiePath = "/")
        : authManager_(std::move(authManager)),
          registrationEnabled_(registrationEnabled),
          secureCookies_(secureCookies),
          sameSite_(std::move(sameSite)),
          cookiePath_(std::move(cookiePath)) {}
};

inline std::shared_ptr<AuthApiHandler> makeAuthApiHandler(
    std::shared_ptr<qornix_auth::AuthManager> authManager,
    bool registrationEnabled = false,
    bool secureCookies = false,
    std::string sameSite = "Lax",
    std::string cookiePath = "/") {
    return std::make_shared<AuthApiHandler>(
        std::move(authManager),
        registrationEnabled,
        secureCookies,
        std::move(sameSite),
        std::move(cookiePath));
}

inline void setupAuthRoutes(HttpServer& server,
                            std::shared_ptr<qornix_auth::AuthManager> authManager,
                            bool registrationEnabled = false,
                            bool secureCookies = false,
                            std::string sameSite = "Lax",
                            std::string cookiePath = "/") {
    auto handler = makeAuthApiHandler(
        std::move(authManager),
        registrationEnabled,
        secureCookies,
        std::move(sameSite),
        std::move(cookiePath));
    server.add_route("/auth/login", handler);
    server.add_route("/auth/logout", handler);
    server.add_route("/auth/me", handler);
    server.add_route("/auth/register", handler);
    server.add_route("/auth/users", handler);
    server.add_route("/auth/users/{id}", handler);
    server.add_route("/auth/invites", handler);
    server.add_route("/auth/invites/accept", handler);
    server.add_route("/auth/password-reset/request", handler);
    server.add_route("/auth/password-reset/confirm", handler);
    server.add_route("/auth/audit", handler);
}
