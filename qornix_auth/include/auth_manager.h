/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "auth_interface.h"
#include "user.h"
#include "password_hasher.h"
#include "session.h"
#include "jwt_token.h"
#include <map>
#include <mutex>
#include <shared_mutex>
#include <random>

namespace qornix_auth {

enum class AuthMode {
    SESSION,
    JWT,
    BOTH
};

struct AuthConfig {
    AuthMode mode = AuthMode::BOTH;
    std::chrono::minutes sessionDuration = std::chrono::minutes(30);
    std::chrono::minutes jwtDuration = std::chrono::minutes(60);
    std::string jwtSecret = "change-this-secret-key-in-production";
    std::string jwtIssuer = "qornix-auth";
    bool enablePasswordValidation = true;
    size_t minPasswordLength = 6;
    bool enableUserEnumerationProtection = true;
};

class AuthManager : public AuthInterface {
private:
    mutable std::shared_mutex usersMutex_;
    std::map<std::string, User> users_;
    std::map<std::string, std::string> usernameToId_;

    std::unique_ptr<PasswordHasher> hasher_;
    std::unique_ptr<SessionManager> sessionManager_;
    std::unique_ptr<JwtManager> jwtManager_;

    AuthConfig config_;

    std::string generateUserId() const {
        static std::atomic<int> counter{0};
        return "usr_" + std::to_string(++counter) + "_" +
               std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }

    bool validatePassword(const std::string& password) const {
        if (!config_.enablePasswordValidation) {
            return true;
        }
        return password.length() >= config_.minPasswordLength;
    }

public:
    explicit AuthManager(const AuthConfig& config = AuthConfig())
        : config_(config),
          hasher_(PasswordHasher::createDefault()),
          sessionManager_(std::make_unique<SessionManager>(config.sessionDuration)),
          jwtManager_(std::make_unique<JwtManager>(
              config.jwtSecret, config.jwtIssuer, config.jwtDuration
          )) {}

    AuthResult registerUser(const std::string& username,
                           const std::string& password,
                           const std::string& email = "") override {
        std::unique_lock lock(usersMutex_);

        if (usernameToId_.find(username) != usernameToId_.end()) {
            return AuthResult::failure(AuthStatus::USER_ALREADY_EXISTS,
                                      "User already exists");
        }

        if (!validatePassword(password)) {
            return AuthResult::failure(AuthStatus::ERROR,
                                      "Password does not meet requirements");
        }

        std::string userId = generateUserId();
        std::string salt = hasher_->generateSalt();
        std::string passwordHash = hasher_->hash(password, salt);

        User user(userId, username, email);
        user.password = passwordHash;
        user.salt = salt;

        users_[userId] = user;
        usernameToId_[username] = userId;

        return AuthResult::success(userId);
    }

    AuthResult authenticate(const std::string& username,
                           const std::string& password) override {
        std::shared_lock usersLock(usersMutex_);

        auto it = usernameToId_.find(username);
        if (it == usernameToId_.end()) {
            if (config_.enableUserEnumerationProtection) {
                hasher_->hash(password, "dummy-salt");
            }
            return AuthResult::failure(AuthStatus::USER_NOT_FOUND,
                                      "Invalid credentials");
        }

        const User& user = users_.at(it->second);

        if (!hasher_->verify(password, user.password, user.salt)) {
            return AuthResult::failure(AuthStatus::INVALID_CREDENTIALS,
                                      "Invalid credentials");
        }

        AuthResult result = AuthResult::success(user.id);

        if (config_.mode == AuthMode::SESSION || config_.mode == AuthMode::BOTH) {
            auto session = sessionManager_->createSession(user.id, user.username);
            result.message += "|session:" + session->sessionId;
        }

        if (config_.mode == AuthMode::JWT || config_.mode == AuthMode::BOTH) {
            auto tokenResult = jwtManager_->generateToken(user.id, user.username);
            if (tokenResult.success) {
                result.message += "|token:" + tokenResult.token;
            }
        }

        return result;
    }

    AuthResult logout(const std::string& userId) override {
        sessionManager_->cleanupExpiredSessions();
        return AuthResult::success("");
    }

    bool isAuthenticated(const std::string& userId) const override {
        auto user = getUserById(userId);
        return user != nullptr && user->isActive;
    }

    std::optional<std::string> getCurrentUser() const override {
        return std::nullopt;
    }

    std::shared_ptr<SessionData> validateSession(const std::string& sessionId) {
        return sessionManager_->getSession(sessionId);
    }

    JwtTokenResult validateJwtToken(const std::string& token) {
        return jwtManager_->validateToken(token);
    }

    const User* getUserById(const std::string& userId) const {
        std::shared_lock lock(usersMutex_);
        auto it = users_.find(userId);
        if (it != users_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const User* getUserByUsername(const std::string& username) const {
        std::shared_lock lock(usersMutex_);
        auto it = usernameToId_.find(username);
        if (it != usernameToId_.end()) {
            return &users_.at(it->second);
        }
        return nullptr;
    }

    bool changePassword(const std::string& userId,
                       const std::string& oldPassword,
                       const std::string& newPassword) {
        std::unique_lock lock(usersMutex_);

        auto it = users_.find(userId);
        if (it == users_.end()) {
            return false;
        }

        User& user = it->second;

        if (!hasher_->verify(oldPassword, user.password, user.salt)) {
            return false;
        }

        if (!validatePassword(newPassword)) {
            return false;
        }

        std::string newSalt = hasher_->generateSalt();
        std::string newPasswordHash = hasher_->hash(newPassword, newSalt);

        user.password = newPasswordHash;
        user.salt = newSalt;

        return true;
    }

    void setConfig(const AuthConfig& config) {
        config_ = config;
        jwtManager_->setSecretKey(config.jwtSecret);
        jwtManager_->setTokenDuration(config.jwtDuration);
    }

    const AuthConfig& getConfig() const {
        return config_;
    }
};

} // namespace qornix_auth

