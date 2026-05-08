/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include "handlers.h"
#include "http_server.h"
#include "handler_base.h"
#include "example_paths.h"
#include "dynamic_api_handlers.h"

void setupDynamicRoutes(HttpServer& server) {
    std::cout << "DEBUG: Adding dynamic routes..." << std::endl;

    // Root page with API description
    auto apiDescHandler = std::make_shared<ApiDescriptionHandler>();
    server.add_route("/", apiDescHandler);

    // Universal handler for static files
    std::string staticDir = dynamic_web_query_builder_example::templatesDir();
    auto staticHandler = StaticFileHandler::create(staticDir);
    // server.add_route("/static/styles.css", staticHandler);
    // server.add_route("/app.js", staticHandler);
    // Universal route for any static files from the templates directory
    server.add_route("/static/{filename}", staticHandler);

    // Query Builder interface page
    auto queryInterfaceHandler = std::make_shared<QueryBuilderInterfaceHandler>();
    server.add_route("/query-builder", queryInterfaceHandler);

    // Table Browser page (filtered table view)
    auto tableInterfaceHandler = std::make_shared<TableInterfaceHandler>();
    server.add_route("/table", tableInterfaceHandler);

    // Row View page (detailed table row view)
    auto rowViewInterfaceHandler = std::make_shared<RowViewInterfaceHandler>();
    server.add_route("/table/{table}/{id}", rowViewInterfaceHandler);

    // XML schema management page
    auto schemaManagerPageHandler = std::make_shared<SchemaManagerPageHandler>();
    server.add_route("/schema-manager", schemaManagerPageHandler);

    qornix_dynamic_api::DynamicApiConfig dynamicConfig;
    dynamicConfig.databaseConfig = dynamic_web_query_builder_example::demoDatabaseConfig();
#ifdef QORNIX_ORM_SCHEMA_APP_XSD_PATH
    dynamicConfig.schemaXsdPath = QORNIX_ORM_SCHEMA_APP_XSD_PATH;
#else
    dynamicConfig.schemaXsdPath = "qornix_orm/schema/schema_app.xsd";
#endif
    dynamicConfig.exportedSchemaPath = dynamic_web_query_builder_example::demoSchemaPath().string();
    dynamicConfig.uploadedSchemaPath = dynamic_web_query_builder_example::uploadedSchemaPath().string();
    dynamicConfig.historyPath = (dynamic_web_query_builder_example::sourceDir() / "schema_history.jsonl").string();
    dynamicConfig.defaultLimit = 100;
    dynamicConfig.maxLimit = 1000;
    dynamicConfig.allowRawFilter = false;
    dynamicConfig.allowRawJoin = false;
    dynamicConfig.allowUpdateWithoutFilter = false;
    dynamicConfig.allowDeleteWithoutFilter = false;
    dynamicConfig.requirePermissions = false;

    // Schema-driven Dynamic API module: CRUD/query, metadata, schema manager API, OpenAPI and history.
    qornix_dynamic_api::addSchemaDrivenDynamicApiRoutes(server, dynamicConfig);

    std::cout << "DEBUG: Dynamic routes added successfully" << std::endl;
}
