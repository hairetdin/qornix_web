/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include "http_server.h"
#include "handlers/home_handler.h"
#include "handlers/user_handler.h"
#include <memory>

inline void setupRoutes(HttpServer& srv, DIContainer& container) {
    // Get the handler from the container or create it directly
    // For now, create the handler directly, but pass the container

    auto userHandler = std::make_shared<UserHandler>();
    auto homeHandler = std::make_shared<HomeHandler>();

    srv.add_route("/", homeHandler);
    srv.add_route("/health", homeHandler);
    srv.add_route("/info", homeHandler);
    srv.add_route("/notfound", homeHandler);
    srv.add_route("/users", userHandler);
    srv.add_route("/users/{id}", userHandler);
    srv.add_route("/api/v1/users", userHandler);
    srv.add_route("/api/v1/users/{id}", userHandler);
}

inline void anotherSetupRoutes(HttpServer& srv) {
    auto userHandler = std::make_shared<UserHandler>();
    auto homeHandler = std::make_shared<HomeHandler>();

    srv.add_route("/", homeHandler);
    srv.add_route("/health", homeHandler);
    srv.add_route("/info", homeHandler);
    srv.add_route("/notfound", homeHandler);
    srv.add_route("/users", userHandler);
    srv.add_route("/users/{id}", userHandler);
    srv.add_route("/api/v1/users", userHandler);
    srv.add_route("/api/v1/users/{id}", userHandler);
}
