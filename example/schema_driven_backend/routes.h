/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include "http_server.h"
#include <qornix_dynamic_api/dynamic_api_handlers.h>
inline void setupRoutes(HttpServer& server) {
    qornix_dynamic_api::DynamicApiConfig config;
    config.databaseConfig.driver = "sqlite";
    config.databaseConfig.dbname = "schema_driven_backend.sqlite3";
    config.uploadedSchemaPath = "schema/app.schema.xml";
    config.exportedSchemaPath = "schema/database.schema.xml";
    config.historyPath = "schema/schema_history.jsonl";
    qornix_dynamic_api::addSchemaDrivenDynamicApiRoutes(server, config);
}
