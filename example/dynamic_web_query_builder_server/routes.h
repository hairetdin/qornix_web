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

#include <chrono>
#include <utility>

#if QORNIX_ENABLE_ASYNC_DB
#include "dynamic_api_async_db.h"
#endif

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
    dynamicConfig.dynamicQueryTimeout = std::chrono::milliseconds{2000};
    dynamicConfig.dynamicRouteTimeout = std::chrono::milliseconds{30000};
    dynamicConfig.maxDynamicBodySize = 1024 * 1024;
    dynamicConfig.maxConcurrentDbOperations = 128;
#if QORNIX_ENABLE_ASYNC_DB
    dynamicConfig.preparedDynamicQueries =
        qornix_dynamic_api::dynamicAsyncDbSupportsPreparedQueries(dynamicConfig.databaseConfig);
#else
    dynamicConfig.preparedDynamicQueries = false;
#endif

    // Schema-driven Dynamic API module: CRUD/query, metadata, schema manager API, OpenAPI and history.
#if QORNIX_ENABLE_ASYNC_DB
    qornix_dynamic_api::DynamicAsyncDatabaseOptions asyncOptions;
    asyncOptions.pool.pool_name = "dynamic-web-query-builder";
    asyncOptions.pool.max_connections = 8;
    asyncOptions.pool.max_waiters = 256;
    asyncOptions.pool.acquire_timeout = std::chrono::milliseconds{500};
    asyncOptions.pool.query_timeout = dynamicConfig.dynamicQueryTimeout;
    asyncOptions.offloadedWorkerThreads = 1;

    auto asyncDatabase = qornix_dynamic_api::makeDynamicAsyncDatabaseInterface(
        server.executor(),
        dynamicConfig.databaseConfig,
        asyncOptions);
    auto allowlist = qornix_dynamic_api::loadDynamicAsyncAllowlist(dynamicConfig);
    qornix_dynamic_api::addSchemaDrivenDynamicApiAsyncRoutes(
        server,
        dynamicConfig,
        std::move(asyncDatabase),
        std::move(allowlist));
    std::cout << "DEBUG: Dynamic API CRUD routes registered on async DB path" << std::endl;
#else
    qornix_dynamic_api::addSchemaDrivenDynamicApiRoutes(server, dynamicConfig);
#endif

    std::cout << "DEBUG: Dynamic routes added successfully" << std::endl;
}
