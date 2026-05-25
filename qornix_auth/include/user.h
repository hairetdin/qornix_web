/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <vector>
#include <algorithm>

namespace qornix_auth {
    struct User {
        std::string id;
        std::string username;
        std::string email;
        std::string password;
        std::string salt;
        std::vector<std::string> roles;
        std::vector<std::string> permissions;
        bool isActive;
        std::chrono::system_clock::time_point createdAt;
        std::chrono::system_clock::time_point lastLoginAt;

        User() : isActive(true), createdAt(std::chrono::system_clock::now()),
                 lastLoginAt(std::chrono::system_clock::now()) {}

        User(const std::string& user_id,
             const std::string& user_name,
             const std::string& user_email = "")
            : id(user_id), username(user_name), email(user_email),
              roles({"user"}),
              isActive(true), createdAt(std::chrono::system_clock::now()),
              lastLoginAt(std::chrono::system_clock::now()) {}

        bool hasRole(const std::string& role) const {
            return std::find(roles.begin(), roles.end(), role) != roles.end();
        }

        bool hasPermission(const std::string& permission) const {
            return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
        }
    };
}
