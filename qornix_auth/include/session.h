/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <string>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <utility>


namespace qornix_auth {
    struct SessionData {
        std::string sessionId;
        std::string userId;
        std::string username;
        std::vector<std::string> roles;
        std::vector<std::string> permissions;
        std::chrono::system_clock::time_point createdAt;
        std::chrono::system_clock::time_point expiresAt;
        std::chrono::system_clock::time_point lastAccessedAt;
        std::unordered_map<std::string, std::string> metadata;
        bool isValid;

        SessionData() : isValid(false) {
        }

        SessionData(const std::string &session_id,
                    const std::string &user_id,
                    const std::string &user_name,
                    std::vector<std::string> user_roles = {},
                    std::vector<std::string> user_permissions = {},
                    std::chrono::minutes duration = std::chrono::minutes(30))
            : sessionId(session_id), userId(user_id), username(user_name),
              roles(std::move(user_roles)), permissions(std::move(user_permissions)),
              createdAt(std::chrono::system_clock::now()),
              lastAccessedAt(std::chrono::system_clock::now()),
              isValid(true) {
            expiresAt = createdAt + duration;
        }

        bool isExpired() const {
            return std::chrono::system_clock::now() > expiresAt;
        }

        void extend(std::chrono::minutes duration = std::chrono::minutes(30)) {
            expiresAt = std::chrono::system_clock::now() + duration;
            lastAccessedAt = std::chrono::system_clock::now();
        }
    };

    class SessionManager {
    private:
        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<SessionData> > sessions_;
        std::chrono::minutes defaultDuration_;

        static std::string bytesToHex(const unsigned char* bytes, size_t length) {
            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            for (size_t i = 0; i < length; ++i) {
                ss << std::setw(2) << static_cast<int>(bytes[i]);
            }
            return ss.str();
        }

        static std::string generateSecureToken(const std::string& prefix) {
            unsigned char random[32];
            if (RAND_bytes(random, sizeof(random)) != 1) {
                throw std::runtime_error("Failed to generate secure token");
            }
            return prefix + bytesToHex(random, sizeof(random));
        }

        static std::string generateSessionId() {
            return generateSecureToken("sess_");
        }

    public:
        explicit SessionManager(std::chrono::minutes defaultDuration = std::chrono::minutes(30))
            : defaultDuration_(defaultDuration) {
        }

        std::shared_ptr<SessionData> createSession(const std::string &userId,
                                                   const std::string &username,
                                                   std::vector<std::string> roles = {},
                                                   std::vector<std::string> permissions = {}) {
            std::lock_guard<std::mutex> lock(mutex_);

            auto session = std::make_shared<SessionData>(
                generateSessionId(), userId, username, std::move(roles), std::move(permissions), defaultDuration_
            );
            session->metadata["csrf_token"] = generateSecureToken("csrf_");
            sessions_[session->sessionId] = session;
            return session;
        }

        std::shared_ptr<SessionData> getSession(const std::string &sessionId) {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = sessions_.find(sessionId);
            if (it != sessions_.end()) {
                auto &session = it->second;
                if (!session->isExpired() && session->isValid) {
                    session->extend(defaultDuration_);
                    return session;
                } else {
                    sessions_.erase(it);
                }
            }
            return nullptr;
        }

        bool invalidateSession(const std::string &sessionId) {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = sessions_.find(sessionId);
            if (it != sessions_.end()) {
                it->second->isValid = false;
                sessions_.erase(it);
                return true;
            }
            return false;
        }

        void cleanupExpiredSessions() {
            std::lock_guard<std::mutex> lock(mutex_);

            auto now = std::chrono::system_clock::now();
            for (auto it = sessions_.begin(); it != sessions_.end();) {
                if (it->second->isExpired() || !it->second->isValid) {
                    it = sessions_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        size_t getActiveSessionCount() const {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t count = 0;
            for (const auto &[id, session]: sessions_) {
                if (!session->isExpired() && session->isValid) {
                    ++count;
                }
            }
            return count;
        }
    };
} // namespace qornix_auth
