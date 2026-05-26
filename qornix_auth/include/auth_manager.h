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
#include <map>
#include <memory>
#include <mutex>
#include <openssl/rand.h>
#include <optional>
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

struct AuthAuditEvent {
    std::chrono::system_clock::time_point at = std::chrono::system_clock::now();
    std::string type;
    std::string userId;
    std::string username;
    std::string outcome;
    std::string detail;
};

struct AuthOneTimeToken {
    std::string token;
    std::string type;
    std::string userId;
    std::string username;
    std::string email;
    std::vector<std::string> roles;
    std::vector<std::string> permissions;
    std::chrono::system_clock::time_point expiresAt;
    bool used = false;

    bool isExpired() const {
        return std::chrono::system_clock::now() > expiresAt;
    }
};

class AuthManager : public AuthInterface {
private:
    std::shared_ptr<AuthStore> store_;
    std::unique_ptr<PasswordHasher> hasher_;
    std::unique_ptr<SessionManager> sessionManager_;
    std::unique_ptr<JwtManager> jwtManager_;
    AuthConfig config_;
    mutable std::mutex auditMutex_;
    std::vector<AuthAuditEvent> auditEvents_;
    mutable std::mutex tokenMutex_;
    std::map<std::string, AuthOneTimeToken> oneTimeTokens_;

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

    static std::string generateSecureToken(const std::string& prefix) {
        unsigned char random[32];
        if (RAND_bytes(random, sizeof(random)) != 1) {
            static std::atomic<int> fallbackCounter{0};
            return prefix + "fallback_" + std::to_string(++fallbackCounter);
        }
        return prefix + bytesToHex(random, sizeof(random));
    }

    void audit(std::string type,
               std::string userId,
               std::string username,
               std::string outcome,
               std::string detail = "") {
        std::lock_guard lock(auditMutex_);
        auditEvents_.push_back(AuthAuditEvent{
            std::chrono::system_clock::now(),
            std::move(type),
            std::move(userId),
            std::move(username),
            std::move(outcome),
            std::move(detail)
        });
        if (auditEvents_.size() > 1000) {
            auditEvents_.erase(auditEvents_.begin(), auditEvents_.begin() + static_cast<std::ptrdiff_t>(auditEvents_.size() - 1000));
        }
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
            audit("register", "", username, "failure", "duplicate");
            return AuthResult::failure(AuthStatus::USER_ALREADY_EXISTS, "User already exists");
        }

        if (!validatePassword(password)) {
            audit("register", "", username, "failure", "weak_password");
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
            audit("register", userId, username, "failure", "store_create_failed");
            return AuthResult::failure(AuthStatus::USER_ALREADY_EXISTS, "User already exists");
        }
        audit("register", userId, username, "success");

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
            audit("login", "", username, "failure", "not_found");
            return AuthResult::failure(AuthStatus::USER_NOT_FOUND, "Invalid credentials");
        }

        if (!user->isActive) {
            audit("login", user->id, user->username, "failure", "inactive");
            return AuthResult::failure(AuthStatus::INVALID_CREDENTIALS, "Invalid credentials");
        }

        if (!hasher_->verify(password, user->password, user->salt)) {
            audit("login", user->id, user->username, "failure", "invalid_password");
            return AuthResult::failure(AuthStatus::INVALID_CREDENTIALS, "Invalid credentials");
        }

        user->lastLoginAt = std::chrono::system_clock::now();
        store_->updateUser(*user);

        auto result = buildSuccessResult(*user);
        if (!result.sessionId.empty()) {
            sessionManager_->invalidateSessionsForUser(user->id, result.sessionId);
        }
        audit("login", user->id, user->username, "success");
        return result;
    }

    AuthResult logout(const std::string& sessionIdOrUserId) override {
        sessionManager_->invalidateSession(sessionIdOrUserId);
        sessionManager_->cleanupExpiredSessions();
        audit("logout", sessionIdOrUserId, "", "success");
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
            audit("session_denied", session->userId, session->username, "failure", "inactive_or_missing_user");
            return std::nullopt;
        }

        AuthContext context;
        context.authenticated = true;
        context.userId = user->id;
        context.username = user->username;
        context.sessionId = session->sessionId;
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
        const bool updated = store_->updateUser(*user);
        if (updated) {
            sessionManager_->invalidateSessionsForUser(userId);
            audit("password_change", userId, user->username, "success");
        }
        return updated;
    }

    size_t invalidateSessionsForUser(const std::string& userId,
                                     const std::string& exceptSessionId = "") {
        const auto removed = sessionManager_->invalidateSessionsForUser(userId, exceptSessionId);
        if (removed > 0) {
            audit("session_invalidate_user", userId, "", "success", std::to_string(removed));
        }
        return removed;
    }

    void recordAuditEvent(const std::string& type,
                          const std::string& userId,
                          const std::string& username,
                          const std::string& outcome,
                          const std::string& detail = "") {
        audit(type, userId, username, outcome, detail);
    }

    std::string createInviteToken(const std::string& username,
                                  const std::string& email,
                                  std::vector<std::string> roles,
                                  std::vector<std::string> permissions,
                                  std::chrono::minutes ttl = std::chrono::minutes(1440)) {
        AuthOneTimeToken record;
        record.token = generateSecureToken("invite_");
        record.type = "invite";
        record.username = username;
        record.email = email;
        record.roles = std::move(roles);
        record.permissions = std::move(permissions);
        record.expiresAt = std::chrono::system_clock::now() + ttl;
        std::lock_guard lock(tokenMutex_);
        oneTimeTokens_[record.token] = record;
        audit("invite_create", "", username, "success");
        return record.token;
    }

    AuthResult acceptInviteToken(const std::string& token,
                                 const std::string& password,
                                 const std::string& usernameOverride = "",
                                 const std::string& emailOverride = "") {
        AuthOneTimeToken record;
        {
            std::lock_guard lock(tokenMutex_);
            auto it = oneTimeTokens_.find(token);
            if (it == oneTimeTokens_.end() || it->second.type != "invite" || it->second.used || it->second.isExpired()) {
                audit("invite_accept", "", "", "failure", "invalid_or_expired");
                return AuthResult::failure(AuthStatus::INVALID_CREDENTIALS, "Invalid or expired invite token");
            }
            it->second.used = true;
            record = it->second;
        }

        auto result = registerUser(
            usernameOverride.empty() ? record.username : usernameOverride,
            password,
            emailOverride.empty() ? record.email : emailOverride,
            record.roles,
            record.permissions);
        audit("invite_accept", result.userId, result.username, result.status == AuthStatus::SUCCESS ? "success" : "failure");
        return result;
    }

    std::string createPasswordResetToken(const std::string& username,
                                         std::chrono::minutes ttl = std::chrono::minutes(60)) {
        auto user = store_->findUserByUsername(username);
        if (!user.has_value() || !user->isActive) {
            audit("password_reset_create", "", username, "failure", "missing_or_inactive");
            return "";
        }
        AuthOneTimeToken record;
        record.token = generateSecureToken("reset_");
        record.type = "password_reset";
        record.userId = user->id;
        record.username = user->username;
        record.email = user->email;
        record.expiresAt = std::chrono::system_clock::now() + ttl;
        std::lock_guard lock(tokenMutex_);
        oneTimeTokens_[record.token] = record;
        audit("password_reset_create", user->id, user->username, "success");
        return record.token;
    }

    bool resetPasswordWithToken(const std::string& token, const std::string& newPassword) {
        AuthOneTimeToken record;
        {
            std::lock_guard lock(tokenMutex_);
            auto it = oneTimeTokens_.find(token);
            if (it == oneTimeTokens_.end() || it->second.type != "password_reset" || it->second.used || it->second.isExpired()) {
                audit("password_reset_confirm", "", "", "failure", "invalid_or_expired");
                return false;
            }
            it->second.used = true;
            record = it->second;
        }
        auto user = store_->findUserById(record.userId);
        if (!user.has_value() || !validatePassword(newPassword)) {
            audit("password_reset_confirm", record.userId, record.username, "failure", "invalid_user_or_password");
            return false;
        }
        user->salt = hasher_->generateSalt();
        user->password = hasher_->hash(newPassword, user->salt);
        const bool updated = store_->updateUser(*user);
        if (updated) {
            sessionManager_->invalidateSessionsForUser(user->id);
            audit("password_reset_confirm", user->id, user->username, "success");
        }
        return updated;
    }

    std::vector<AuthAuditEvent> auditEvents() const {
        std::lock_guard lock(auditMutex_);
        return auditEvents_;
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

    std::string csrfTokenForSession(const std::string& sessionId) {
        auto session = validateSession(sessionId);
        if (!session) {
            return "";
        }
        auto it = session->metadata.find("csrf_token");
        return it == session->metadata.end() ? "" : it->second;
    }

    bool validateCsrfToken(const std::string& sessionId, const std::string& csrfToken) {
        if (sessionId.empty() || csrfToken.empty()) {
            return false;
        }
        return csrfTokenForSession(sessionId) == csrfToken;
    }
};

} // namespace qornix_auth
