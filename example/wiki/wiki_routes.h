/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "http_server.h"
#include "handler_base.h"
#include "wiki_handlers.h"
#include "wiki_auth_handlers.h"
#include "logging_middleware.h"

#include <memory>
#include <string>

inline void setupWikiRoutes(HttpServer& srv, const wiki_example::WikiConfig& config = wiki_example::WikiConfig{}) {
    // Create database connection for wiki repository
    auto dbConfig = wiki_example::databaseConfig(config);
    auto dbUnique = DatabaseInterface::init(dbConfig);
    auto db = std::shared_ptr<DatabaseInterface>(dbUnique.release());

    // Load ORM schema (includes Users entity from wiki_schema.xml)
    SchemaLoader::loadSchemaFromFile(wiki_example::schemaPath(config));

    // Create AuthManager as shared_ptr to live beyond this function
    auto authManager = std::make_shared<qornix_auth::AuthManager>(qornix_auth::AuthConfig{});
    qornix_auth::AuthConfig authConfig;
    authConfig.mode = qornix_auth::AuthMode::BOTH;
    authConfig.sessionDuration = std::chrono::minutes(config.auth_session_duration_minutes);
    authConfig.jwtDuration = std::chrono::minutes(config.auth_jwt_duration_minutes);
    authConfig.jwtSecret = config.auth_jwt_secret;
    authConfig.enablePasswordValidation = config.auth_password_validation;
    authConfig.minPasswordLength = config.auth_min_password_length;
    authManager->setConfig(authConfig);

    // Create auth handlers (shared_ptr<DatabaseInterface> passed from here)
    auto registerHandler = std::make_shared<wiki_auth::RegisterHandler>(authManager, db);
    auto loginHandler = std::make_shared<wiki_auth::LoginHandler>(authManager, db);
    auto meHandler = std::make_shared<wiki_auth::MeHandler>(authManager);

    // Create wiki handlers
    auto repository = std::make_shared<wiki_example::WikiRepository>(config);
    auto pageHandler = std::make_shared<wiki_example::WikiPageHandler>();
    auto apiHandler = std::make_shared<wiki_example::WikiApiHandler>(repository);
    auto healthHandler = std::make_shared<wiki_example::WikiHealthHandler>(repository);
    auto staticHandler = StaticFileHandler::create(wiki_example::staticDirectory());

    // Register auth routes (no auth required)
    srv.add_route("/api/auth/login", loginHandler);
    srv.add_route("/api/auth/register", registerHandler);
    srv.add_route("/api/auth/me", meHandler);

    // Register wiki routes
    srv.add_route("/", pageHandler);
    srv.add_route("/article/{id}", std::make_shared<wiki_example::WikiDetailPageHandler>(repository));
    srv.add_route("/health", healthHandler);
    srv.add_route("/static/{filename}", staticHandler);

    srv.add_route("/api/wiki/stats", apiHandler);
    srv.add_route("/api/wiki/categories", apiHandler);
    srv.add_route("/api/wiki/activity", apiHandler);
    srv.add_route("/api/wiki/articles", apiHandler);
    srv.add_route("/api/wiki/articles/{id}", apiHandler);
    srv.add_route("/api/wiki/articles/{id}/favorite", apiHandler);

    // Create and add auth middleware
    // Protect all routes except auth endpoints, health, and static files
    auto authMiddleware = create_auth_middleware(
        authManager,
        true,  // auth required
        {"/api/auth/login", "/api/auth/register", "/health", "/static/", "/", "/article/"}
    );

    // Add logging middleware (logs all requests)
    auto loggingMiddleware = create_logging_middleware(true);
    srv.add_middleware(loggingMiddleware);

    // Add auth middleware (applies to all routes)
    srv.add_middleware(authMiddleware);
}
