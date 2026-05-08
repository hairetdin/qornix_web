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
#include <atomic>


namespace qornix_auth {
    struct SessionData {
        std::string sessionId;
        std::string userId;
        std::string username;
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
                    std::chrono::minutes duration = std::chrono::minutes(30))
            : sessionId(session_id), userId(user_id), username(user_name),
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

        static std::string generateSessionId() {
            static std::atomic<int> counter{0};
            auto now = std::chrono::system_clock::now().time_since_epoch().count();
            return "sess_" + std::to_string(now) + "_" + std::to_string(++counter);
        }

    public:
        explicit SessionManager(std::chrono::minutes defaultDuration = std::chrono::minutes(30))
            : defaultDuration_(defaultDuration) {
        }

        std::shared_ptr<SessionData> createSession(const std::string &userId,
                                                   const std::string &username) {
            std::lock_guard<std::mutex> lock(mutex_);

            auto session = std::make_shared<SessionData>(
                generateSessionId(), userId, username, defaultDuration_
            );
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
