/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "user.h"

#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace qornix_auth {

class AuthStore {
public:
    virtual ~AuthStore() = default;

    virtual bool createUser(const User& user) = 0;
    virtual bool updateUser(const User& user) = 0;
    virtual std::optional<User> findUserById(const std::string& userId) const = 0;
    virtual std::optional<User> findUserByUsername(const std::string& username) const = 0;
    virtual std::vector<User> listUsers() const = 0;
};

class InMemoryAuthStore : public AuthStore {
private:
    mutable std::shared_mutex mutex_;
    std::map<std::string, User> users_;
    std::map<std::string, std::string> usernameToId_;

public:
    bool createUser(const User& user) override {
        std::unique_lock lock(mutex_);
        if (user.id.empty() || user.username.empty() || usernameToId_.count(user.username) > 0 || users_.count(user.id) > 0) {
            return false;
        }
        users_[user.id] = user;
        usernameToId_[user.username] = user.id;
        return true;
    }

    bool updateUser(const User& user) override {
        std::unique_lock lock(mutex_);
        auto current = users_.find(user.id);
        if (current == users_.end()) {
            return false;
        }

        if (current->second.username != user.username) {
            if (usernameToId_.count(user.username) > 0) {
                return false;
            }
            usernameToId_.erase(current->second.username);
            usernameToId_[user.username] = user.id;
        }

        current->second = user;
        return true;
    }

    std::optional<User> findUserById(const std::string& userId) const override {
        std::shared_lock lock(mutex_);
        auto it = users_.find(userId);
        if (it == users_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<User> findUserByUsername(const std::string& username) const override {
        std::shared_lock lock(mutex_);
        auto id = usernameToId_.find(username);
        if (id == usernameToId_.end()) {
            return std::nullopt;
        }
        auto user = users_.find(id->second);
        if (user == users_.end()) {
            return std::nullopt;
        }
        return user->second;
    }

    std::vector<User> listUsers() const override {
        std::shared_lock lock(mutex_);
        std::vector<User> result;
        result.reserve(users_.size());
        for (const auto& [id, user] : users_) {
            result.push_back(user);
        }
        return result;
    }
};

} // namespace qornix_auth
