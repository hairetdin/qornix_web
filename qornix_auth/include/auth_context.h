/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace qornix_auth {

struct AuthContext {
    bool authenticated{false};
    std::string userId;
    std::string username;
    std::string sessionId;
    std::vector<std::string> roles;
    std::vector<std::string> permissions;
    std::string credentialType;

    bool hasRole(const std::string& role) const {
        return std::find(roles.begin(), roles.end(), role) != roles.end();
    }

    bool hasPermission(const std::string& permission) const {
        return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
    }

    bool hasAnyPermission(const std::vector<std::string>& required) const {
        if (required.empty()) {
            return true;
        }
        return std::any_of(required.begin(), required.end(), [this](const std::string& permission) {
            return hasPermission(permission);
        });
    }

    bool hasAnyRole(const std::vector<std::string>& required) const {
        if (required.empty()) {
            return true;
        }
        return std::any_of(required.begin(), required.end(), [this](const std::string& role) {
            return hasRole(role);
        });
    }
};

} // namespace qornix_auth
