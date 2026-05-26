/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include "middleware.h"
#include "auth_manager.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>
#include <algorithm>
#include <cctype>

struct AuthRoutePolicy {
    std::string method;      // Empty or "*" matches any method.
    std::string path;        // Prefix or exact path.
    bool exact{false};
    bool authentication_required{true};
    std::vector<std::string> any_roles;
    std::vector<std::string> any_permissions;
};

class AuthMiddleware : public Middleware {
private:
    std::shared_ptr<qornix_auth::AuthManager> authManager_;
    bool required_;
    std::vector<std::string> excludePaths_;
    std::vector<std::string> requiredRoles_;
    std::vector<std::string> requiredPermissions_;
    std::vector<AuthRoutePolicy> routePolicies_;
    bool enableLogging_;
    bool csrfProtectionEnabled_{false};
    std::string csrfHeaderName_{"X-CSRF-Token"};

    static std::string trim(std::string value) {
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
            return !std::isspace(c);
        }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
            return !std::isspace(c);
        }).base(), value.end());
        return value;
    }

    static std::string cookieValue(const std::string& cookies, const std::string& name) {
        std::stringstream ss(cookies);
        std::string item;
        while (std::getline(ss, item, ';')) {
            item = trim(item);
            const auto eq = item.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            if (item.substr(0, eq) == name) {
                return item.substr(eq + 1);
            }
        }
        return "";
    }

    std::string extractSessionId(const http::request<http::string_body> &req) const {
        auto cookieHeader = req.find("Cookie");
        if (cookieHeader != req.end()) {
            return cookieValue(std::string(cookieHeader->value()), "session_id");
        }
        return "";
    }

    std::string extractBearerToken(const http::request<http::string_body> &req) const {
        auto authHeader = req.find("Authorization");
        if (authHeader != req.end()) {
            std::string authValue = std::string(authHeader->value());
            if (authValue.substr(0, 7) == "Bearer ") {
                return authValue.substr(7);
            }
        }
        return "";
    }

    std::string csrfToken(const http::request<http::string_body> &req) const {
        auto header = req.find(csrfHeaderName_);
        if (header != req.end()) {
            return std::string(header->value());
        }
        return "";
    }

    static bool isUnsafeMethod(const std::string& method) {
        return method == "POST" || method == "PUT" || method == "PATCH" || method == "DELETE";
    }

    bool isExcluded(const std::string &path) const {
        for (const auto &excluded: excludePaths_) {
            if (prefixPathMatches(excluded, path)) {
                return true;
            }
        }
        return false;
    }

    static bool hasAny(const std::vector<std::string>& actual, const std::vector<std::string>& required) {
        if (required.empty()) {
            return true;
        }
        return std::any_of(required.begin(), required.end(), [&actual](const std::string& expected) {
            return std::find(actual.begin(), actual.end(), expected) != actual.end();
        });
    }

    static bool methodMatches(const AuthRoutePolicy& policy, const std::string& method) {
        return policy.method.empty() || policy.method == "*" || policy.method == method;
    }

    static bool prefixPathMatches(const std::string& prefix, const std::string& path) {
        if (prefix.empty() || path.rfind(prefix, 0) != 0) {
            return false;
        }
        if (path.size() == prefix.size()) {
            return true;
        }
        if (prefix == "/") {
            return true;
        }
        return path[prefix.size()] == '/';
    }

    static bool pathMatches(const AuthRoutePolicy& policy, const std::string& path) {
        if (policy.path.empty()) {
            return false;
        }
        if (policy.exact) {
            return path == policy.path;
        }
        return prefixPathMatches(policy.path, path);
    }

    std::optional<AuthRoutePolicy> matchingPolicy(const std::string& method, const std::string& path) const {
        for (const auto& policy : routePolicies_) {
            if (methodMatches(policy, method) && pathMatches(policy, path)) {
                return policy;
            }
        }
        return std::nullopt;
    }

    static std::string join(const std::vector<std::string>& values) {
        std::string result;
        for (const auto& value : values) {
            if (!result.empty()) {
                result += ",";
            }
            result += value;
        }
        return result;
    }

    void logMessage(const std::string &level, const std::string &message) const {
        if (!enableLogging_) {
            return;
        }

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");

        std::cerr << "[" << ss.str() << "] [" << level << "] AuthMiddleware: " << message << std::endl;
    }

public:
    AuthMiddleware(std::shared_ptr<qornix_auth::AuthManager> authManager,
                   bool required = true,
                   const std::vector<std::string> &excludePaths = {},
                   bool enableLogging = false)
        : authManager_(std::move(authManager)), required_(required), excludePaths_(excludePaths),
          enableLogging_(enableLogging) {
    }

    void handle(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params,
        NextHandler next
    ) override {
        std::string path = std::string(url_view.path());
        std::string method = std::string(req.method_string());
        const auto policy = matchingPolicy(method, path);
        const bool authenticationRequired = policy ? policy->authentication_required : required_;
        const auto& requiredRoles = policy ? policy->any_roles : requiredRoles_;
        const auto& requiredPermissions = policy ? policy->any_permissions : requiredPermissions_;

        if (isExcluded(path)) {
            logMessage("DEBUG", "Path excluded from auth: " + path);
            next();
            return;
        }

        std::string sessionId = extractSessionId(req);
        std::string bearerToken = extractBearerToken(req);

        qornix_auth::AuthContext authContext;

        if (!sessionId.empty()) {
            logMessage("DEBUG", "Found session ID: " + sessionId);
            auto context = authManager_->authenticateSession(sessionId);
            if (context.has_value()) {
                authContext = *context;
                logMessage("INFO", "Session validated for user: " + authContext.username);
            } else {
                logMessage("WARNING", "Invalid or expired session");
            }
        }

        if (!authContext.authenticated && !bearerToken.empty()) {
            logMessage("DEBUG", "Found Bearer token");
            auto context = authManager_->authenticateBearerToken(bearerToken);
            if (context.has_value()) {
                authContext = *context;
                logMessage("INFO", "JWT token validated for user: " + authContext.userId);
            } else {
                logMessage("WARNING", "Invalid JWT token");
            }
        }

        if (authenticationRequired && !authContext.authenticated) {
            logMessage("ERROR", "Authentication required for path: " + path);
            authManager_->recordAuditEvent("access_denied", "", "", "failure", method + " " + path + " authentication_required");

            res.result(http::status::unauthorized);
            res.set(http::field::content_type, "application/json");

            boost::json::object error_response;
            error_response["error"] = "Unauthorized";
            error_response["message"] = "Authentication required";
            error_response["path"] = path;
            error_response["method"] = method;

            res.body() = boost::json::serialize(error_response);
            res.prepare_payload();
            return;
        }

        if (csrfProtectionEnabled_ &&
            authContext.authenticated &&
            authContext.credentialType == "session" &&
            isUnsafeMethod(method) &&
            !authManager_->validateCsrfToken(authContext.sessionId, csrfToken(req))) {
            logMessage("ERROR", "CSRF token validation failed for path: " + path);
            authManager_->recordAuditEvent("access_denied", authContext.userId, authContext.username, "failure", method + " " + path + " csrf");

            res.result(http::status::forbidden);
            res.set(http::field::content_type, "application/json");

            boost::json::object error_response;
            error_response["error"] = "Forbidden";
            error_response["message"] = "CSRF token required";
            error_response["path"] = path;
            error_response["method"] = method;

            res.body() = boost::json::serialize(error_response);
            res.prepare_payload();
            return;
        }

        if (authContext.authenticated &&
            (!hasAny(authContext.roles, requiredRoles) ||
             !hasAny(authContext.permissions, requiredPermissions))) {
            logMessage("ERROR", "Authorization failed for path: " + path);
            authManager_->recordAuditEvent("access_denied", authContext.userId, authContext.username, "failure", method + " " + path + " authorization");

            res.result(http::status::forbidden);
            res.set(http::field::content_type, "application/json");

            boost::json::object error_response;
            error_response["error"] = "Forbidden";
            error_response["message"] = "Insufficient role or permission";
            error_response["path"] = path;
            error_response["method"] = method;

            res.body() = boost::json::serialize(error_response);
            res.prepare_payload();
            return;
        }

        if (authContext.authenticated) {
            res.set("X-User-ID", authContext.userId);
            if (!authContext.username.empty()) {
                res.set("X-Username", authContext.username);
            }
            res.set("X-Authenticated", "true");
            res.set("X-User-Roles", join(authContext.roles));
            res.set("X-User-Permissions", join(authContext.permissions));
            res.set("X-Auth-Credential-Type", authContext.credentialType);
            logMessage("DEBUG", "User authenticated: " + authContext.userId);
        } else {
            res.set("X-Authenticated", "false");
        }

        next();
    }

    void addExcludePath(const std::string &path) {
        excludePaths_.push_back(path);
        logMessage("DEBUG", "Added exclude path: " + path);
    }

    void setEnableLogging(bool enable) {
        enableLogging_ = enable;
    }

    void setCsrfProtectionEnabled(bool enabled) {
        csrfProtectionEnabled_ = enabled;
    }

    void setCsrfHeaderName(std::string headerName) {
        if (!headerName.empty()) {
            csrfHeaderName_ = std::move(headerName);
        }
    }

    bool isRequired() const {
        return required_;
    }

    void setRequired(bool required) {
        required_ = required;
    }

    void requireAnyRole(std::vector<std::string> roles) {
        requiredRoles_ = std::move(roles);
    }

    void requireAnyPermission(std::vector<std::string> permissions) {
        requiredPermissions_ = std::move(permissions);
    }

    void addRoutePolicy(AuthRoutePolicy policy) {
        routePolicies_.push_back(std::move(policy));
    }

    void addRoutePolicy(std::string method,
                        std::string path,
                        std::vector<std::string> anyPermissions,
                        std::vector<std::string> anyRoles = {},
                        bool exact = false,
                        bool authenticationRequired = true) {
        AuthRoutePolicy policy;
        policy.method = std::move(method);
        policy.path = std::move(path);
        policy.exact = exact;
        policy.authentication_required = authenticationRequired;
        policy.any_permissions = std::move(anyPermissions);
        policy.any_roles = std::move(anyRoles);
        addRoutePolicy(std::move(policy));
    }
};

inline std::shared_ptr<AuthMiddleware> create_auth_middleware(
    std::shared_ptr<qornix_auth::AuthManager> authManager,
    bool required = true,
    const std::vector<std::string> &excludePaths = {},
    bool enableLogging = false) {
    return std::make_shared<AuthMiddleware>(authManager, required, excludePaths, enableLogging);
}
