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
#include <sstream>
#include <utility>
#include <vector>

class AuthMiddleware : public Middleware {
private:
    std::shared_ptr<qornix_auth::AuthManager> authManager_;
    bool required_;
    std::vector<std::string> excludePaths_;
    bool enableLogging_;

    std::string extractSessionId(const http::request<http::string_body> &req) const {
        auto cookieHeader = req.find("Cookie");
        if (cookieHeader != req.end()) {
            std::string cookieStr = std::string(cookieHeader->value());
            size_t pos = cookieStr.find("session_id=");
            if (pos != std::string::npos) {
                size_t start = pos + 11;
                size_t end = cookieStr.find(';', start);
                if (end == std::string::npos) {
                    end = cookieStr.length();
                }
                return cookieStr.substr(start, end - start);
            }
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

    bool isExcluded(const std::string &path) const {
        for (const auto &excluded: excludePaths_) {
            if (path.find(excluded) == 0) {
                return true;
            }
        }
        return false;
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

        if (isExcluded(path)) {
            logMessage("DEBUG", "Path excluded from auth: " + path);
            next();
            return;
        }

        std::string sessionId = extractSessionId(req);
        std::string bearerToken = extractBearerToken(req);

        bool authenticated = false;
        std::string userId;
        std::string username;

        if (!sessionId.empty()) {
            logMessage("DEBUG", "Found session ID: " + sessionId);
            auto session = authManager_->validateSession(sessionId);
            if (session && !session->isExpired()) {
                authenticated = true;
                userId = session->userId;
                username = session->username;
                logMessage("INFO", "Session validated for user: " + username);
            } else {
                logMessage("WARNING", "Invalid or expired session");
            }
        }

        if (!authenticated && !bearerToken.empty()) {
            logMessage("DEBUG", "Found Bearer token");
            auto tokenResult = authManager_->validateJwtToken(bearerToken);
            if (tokenResult.success && tokenResult.claims) {
                authenticated = true;
                userId = tokenResult.claims->subject;
                auto it = tokenResult.claims->customClaims.find("username");
                if (it != tokenResult.claims->customClaims.end()) {
                    username = it->second;
                }
                logMessage("INFO", "JWT token validated for user: " + userId);
            } else {
                logMessage("WARNING", "Invalid JWT token: " + tokenResult.error);
            }
        }

        if (required_ && !authenticated) {
            logMessage("ERROR", "Authentication required for path: " + path);

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

        if (authenticated) {
            res.set("X-User-ID", userId);
            if (!username.empty()) {
                res.set("X-Username", username);
            }
            res.set("X-Authenticated", "true");
            logMessage("DEBUG", "User authenticated: " + userId);
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

    bool isRequired() const {
        return required_;
    }

    void setRequired(bool required) {
        required_ = required;
    }
};

inline std::shared_ptr<AuthMiddleware> create_auth_middleware(
    std::shared_ptr<qornix_auth::AuthManager> authManager,
    bool required = true,
    const std::vector<std::string> &excludePaths = {},
    bool enableLogging = false) {
    return std::make_shared<AuthMiddleware>(authManager, required, excludePaths, enableLogging);
}
