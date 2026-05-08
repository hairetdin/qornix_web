/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <string>
#include <memory>
#include <optional>

namespace qornix_auth {
    enum class AuthStatus {
        SUCCESS,
        INVALID_CREDENTIALS,
        USER_NOT_FOUND,
        USER_ALREADY_EXISTS,
        SESSION_EXPIRED,
        SESSION_INVALID,
        TOKEN_EXPIRED,
        TOKEN_INVALID,
        ERROR
    };

    struct AuthResult {
        AuthStatus status;
        std::string message;
        std::string userId;

        static AuthResult success(const std::string& user_id) {
            return {AuthStatus::SUCCESS, "Authentication successful", user_id};
        }

        static AuthResult failure(AuthStatus status, const std::string& message) {
            return {status, message, ""};
        }
    };

    class AuthInterface {
    public:
        virtual ~AuthInterface() = default;

        virtual AuthResult registerUser(const std::string& username,
                                       const std::string& password,
                                       const std::string& email = "") = 0;

        virtual AuthResult authenticate(const std::string& username,
                                       const std::string& password) = 0;

        virtual AuthResult logout(const std::string& userId) = 0;

        virtual bool isAuthenticated(const std::string& userId) const = 0;

        virtual std::optional<std::string> getCurrentUser() const = 0;
    };
}