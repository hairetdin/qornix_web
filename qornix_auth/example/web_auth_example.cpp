/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "auth_manager.h"
#include "../../include/auth_middleware.h"
#include <iostream>
#include <memory>
#include <boost/asio.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace urls = boost::urls;

void setupAuthExample() {
    qornix_auth::AuthConfig auth_config;
    auth_config.mode = qornix_auth::AuthMode::BOTH;
    auth_config.sessionDuration = std::chrono::minutes(30);
    auth_config.jwtDuration = std::chrono::minutes(60);
    auth_config.jwtSecret = "my-super-secret-key-change-in-production";
    auth_config.minPasswordLength = 12;

    auto authManager = std::make_shared<qornix_auth::AuthManager>(auth_config);

    auto regResult = authManager->registerUser(
        "admin",
        "change-this-admin-password",
        "admin@example.com",
        {"admin"},
        {"rag:read", "rag:write", "rag:admin"});
    if (regResult.status == qornix_auth::AuthStatus::SUCCESS) {
        std::cout << "✓ Admin user registered: " << regResult.userId << "\n";
    }

    auto authMiddleware = create_auth_middleware(
        authManager,
        false,
        {"/api/login", "/api/register", "/health"}
    );

    std::cout << "✓ Auth middleware created\n";
    std::cout << "✓ Exclude paths: /api/login, /api/register, /health\n";
}

int main() {
    try {
        setupAuthExample();
        std::cout << "\n=== Authentication System Ready ===\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
