/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "auth_context.h"
#include "auth_interface.h"
#include "auth_store.h"
#include "jwt_token.h"
#include "password_hasher.h"
#include "session.h"
#include "user.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <string>
#include <utility>
#include <vector>

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
    size_t minPasswordLength = 12;
    bool enableUserEnumerationProtection = true;
    std::vector<std::string> defaultRoles{"user"};
    std::vector<std::string> defaultPermissions{};
};

class AuthManager : public AuthInterface {
private:
    std::shared_ptr<AuthStore> store_;
    std::unique_ptr<PasswordHasher> hasher_;
    std::unique_ptr<SessionManager> sessionManager_;
    std::unique_ptr<JwtManager> jwtManager_;
    AuthConfig config_;

    mutable std::mutex lookupCacheMutex_;
    mutable std::optional<User> lookupCache_;

    static std::string bytesToHex(const unsigned char* bytes, size_t length) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < length; ++i) {
            ss << std::setw(2) << static_cast<int>(bytes[i]);
        }
        return ss.str();
    }

    std::string generateUserId() const {
        unsigned char random[16];
        if (RAND_bytes(random, sizeof(random)) != 1) {
            static std::atomic<int> fallbackCounter{0};
            return "usr_fallback_" + std::to_string(++fallbackCounter);
        }
        return "usr_" + bytesToHex(random, sizeof(random));
    }

    bool validatePassword(const std::string& password) const {
        if (!config_.enablePasswordValidation) {
            return true;
        }
        return password.length() >= config_.minPasswordLength;
    }

    std::optional<User> activeUserById(const std::string& userId) const {
        auto user = store_->findUserById(userId);
        if (!user.has_value() || !user->isActive) {
            return std::nullopt;
        }
        return user;
    }

    AuthResult buildSuccessResult(const User& user) {
        AuthResult result = AuthResult::success(user.id);
        result.username = user.username;
        result.roles = user.roles;
        result.permissions = user.permissions;

        if (config_.mode == AuthMode::SESSION || config_.mode == AuthMode::BOTH) {
            auto session = sessionManager_->createSession(user.id, user.username, user.roles, user.permissions);
            result.sessionId = session->sessionId;
            result.message += "|session:" + session->sessionId;
        }

        if (config_.mode == AuthMode::JWT || config_.mode == AuthMode::BOTH) {
            auto tokenResult = jwtManager_->generateToken(user.id, user.username, {}, user.roles, user.permissions);
            if (tokenResult.success) {
                result.token = tokenResult.token;
                result.message += "|token:" + tokenResult.token;
            }
        }

        return result;
    }

public:
    explicit AuthManager(const AuthConfig& config = AuthConfig(),
                         std::shared_ptr<AuthStore> store = std::make_shared<InMemoryAuthStore>())
        : store_(std::move(store)),
          hasher_(PasswordHasher::createDefault()),
          sessionManager_(std::make_unique<SessionManager>(config.sessionDuration)),
          jwtManager_(std::make_unique<JwtManager>(
              config.jwtSecret, config.jwtIssuer, config.jwtDuration
          )),
          config_(config) {}

    AuthResult registerUser(const std::string& username,
                           const std::string& password,
                           const std::string& email = "") override {
        return registerUser(username, password, email, config_.defaultRoles, config_.defaultPermissions);
    }

    AuthResult registerUser(const std::string& username,
                           const std::string& password,
                           const std::string& email,
                           std::vector<std::string> roles,
                           std::vector<std::string> permissions = {}) {
        if (username.empty()) {
            return AuthResult::failure(AuthStatus::ERROR, "Username is required");
        }

        if (store_->findUserByUsername(username).has_value()) {
            return AuthResult::failure(AuthStatus::USER_ALREADY_EXISTS, "User already exists");
        }

        if (!validatePassword(password)) {
            return AuthResult::failure(AuthStatus::ERROR, "Password does not meet requirements");
        }

        std::string userId = generateUserId();
        std::string salt = hasher_->generateSalt();
        std::string passwordHash = hasher_->hash(password, salt);

        User user(userId, username, email);
        user.password = passwordHash;
        user.salt = salt;
        user.roles = roles.empty() ? config_.defaultRoles : std::move(roles);
        user.permissions = std::move(permissions);

        if (!store_->createUser(user)) {
            return AuthResult::failure(AuthStatus::USER_ALREADY_EXISTS, "User already exists");
        }

        AuthResult result = AuthResult::success(userId);
        result.username = user.username;
        result.roles = user.roles;
        result.permissions = user.permissions;
        return result;
    }

    AuthResult authenticate(const std::string& username,
                           const std::string& password) override {
        auto user = store_->findUserByUsername(username);
        if (!user.has_value()) {
            if (config_.enableUserEnumerationProtection) {
                (void)hasher_->hash(password, "dummy-salt");
            }
            return AuthResult::failure(AuthStatus::USER_NOT_FOUND, "Invalid credentials");
        }

        if (!user->isActive) {
            return AuthResult::failure(AuthStatus::INVALID_CREDENTIALS, "Invalid credentials");
        }

        if (!hasher_->verify(password, user->password, user->salt)) {
            return AuthResult::failure(AuthStatus::INVALID_CREDENTIALS, "Invalid credentials");
        }

        user->lastLoginAt = std::chrono::system_clock::now();
        store_->updateUser(*user);

        return buildSuccessResult(*user);
    }

    AuthResult logout(const std::string& sessionIdOrUserId) override {
        sessionManager_->invalidateSession(sessionIdOrUserId);
        sessionManager_->cleanupExpiredSessions();
        return AuthResult::success("");
    }

    bool isAuthenticated(const std::string& userId) const override {
        auto user = store_->findUserById(userId);
        return user.has_value() && user->isActive;
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

    std::optional<AuthContext> authenticateSession(const std::string& sessionId) {
        auto session = validateSession(sessionId);
        if (!session || session->isExpired()) {
            return std::nullopt;
        }

        auto user = activeUserById(session->userId);
        if (!user.has_value()) {
            sessionManager_->invalidateSession(sessionId);
            return std::nullopt;
        }

        AuthContext context;
        context.authenticated = true;
        context.userId = user->id;
        context.username = user->username;
        context.roles = user->roles;
        context.permissions = user->permissions;
        context.credentialType = "session";
        return context;
    }

    std::optional<AuthContext> authenticateBearerToken(const std::string& token) {
        auto tokenResult = validateJwtToken(token);
        if (!tokenResult.success || !tokenResult.claims) {
            return std::nullopt;
        }

        auto user = activeUserById(tokenResult.claims->subject);
        if (!user.has_value()) {
            return std::nullopt;
        }

        AuthContext context;
        context.authenticated = true;
        context.userId = user->id;
        context.username = user->username;
        context.roles = user->roles;
        context.permissions = user->permissions;
        context.credentialType = "jwt";
        return context;
    }

    bool userHasRole(const std::string& userId, const std::string& role) const {
        auto user = store_->findUserById(userId);
        return user.has_value() && user->hasRole(role);
    }

    bool userHasPermission(const std::string& userId, const std::string& permission) const {
        auto user = store_->findUserById(userId);
        return user.has_value() && user->hasPermission(permission);
    }

    bool assignRole(const std::string& userId, const std::string& role) {
        auto user = store_->findUserById(userId);
        if (!user.has_value() || role.empty()) {
            return false;
        }
        if (!user->hasRole(role)) {
            user->roles.push_back(role);
        }
        return store_->updateUser(*user);
    }

    bool grantPermission(const std::string& userId, const std::string& permission) {
        auto user = store_->findUserById(userId);
        if (!user.has_value() || permission.empty()) {
            return false;
        }
        if (!user->hasPermission(permission)) {
            user->permissions.push_back(permission);
        }
        return store_->updateUser(*user);
    }

    const User* getUserById(const std::string& userId) const {
        auto user = store_->findUserById(userId);
        std::lock_guard lock(lookupCacheMutex_);
        lookupCache_ = std::move(user);
        return lookupCache_ ? &*lookupCache_ : nullptr;
    }

    const User* getUserByUsername(const std::string& username) const {
        auto user = store_->findUserByUsername(username);
        std::lock_guard lock(lookupCacheMutex_);
        lookupCache_ = std::move(user);
        return lookupCache_ ? &*lookupCache_ : nullptr;
    }

    bool changePassword(const std::string& userId,
                       const std::string& oldPassword,
                       const std::string& newPassword) {
        auto user = store_->findUserById(userId);
        if (!user.has_value()) {
            return false;
        }

        if (!hasher_->verify(oldPassword, user->password, user->salt)) {
            return false;
        }

        if (!validatePassword(newPassword)) {
            return false;
        }

        user->salt = hasher_->generateSalt();
        user->password = hasher_->hash(newPassword, user->salt);
        return store_->updateUser(*user);
    }

    void setConfig(const AuthConfig& config) {
        config_ = config;
        jwtManager_->setSecretKey(config.jwtSecret);
        jwtManager_->setTokenDuration(config.jwtDuration);
    }

    const AuthConfig& getConfig() const {
        return config_;
    }

    std::shared_ptr<AuthStore> store() const {
        return store_;
    }
};

} // namespace qornix_auth
