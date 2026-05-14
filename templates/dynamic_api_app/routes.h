#pragma once

#include "app_paths.h"
#include "runtime_config.h"
#include "http_server.h"
#include "handler_base.h"
#include "handlers/dynamic_app_page_handlers.h"

#include <qornix_dynamic_api/dynamic_api_config.h>
#include <qornix_dynamic_api/dynamic_api_handlers.h>
#if QORNIX_ENABLE_ASYNC_DB
#include <qornix_dynamic_api/dynamic_api_async_db.h>
#endif

#include <chrono>
#include <utility>

inline void setupRoutes(HttpServer& server) {
    const auto& runtime = dynamic_api_app_runtime::config();

    qornix_dynamic_api::DynamicApiConfig config;
    config.databaseConfig = runtime.databaseConfig;
    config.uploadedSchemaPath = runtime.uploadedSchemaPath.string();
    config.exportedSchemaPath = runtime.exportedSchemaPath.string();
    config.historyPath = runtime.historyPath.string();
    config.schemaXsdPath = runtime.schemaXsdPath.string();
    config.apiPrefix = runtime.apiPrefix;
    config.schemaPrefix = runtime.schemaPrefix;
    config.defaultLimit = runtime.defaultLimit;
    config.maxLimit = runtime.maxLimit;
    config.allowRawFilter = runtime.allowRawFilter;
    config.allowRawJoin = runtime.allowRawJoin;
    config.allowUpdateWithoutFilter = runtime.allowUpdateWithoutFilter;
    config.allowDeleteWithoutFilter = runtime.allowDeleteWithoutFilter;
    config.allowDestructiveSchemaApply = runtime.allowDestructiveSchemaApply;
    config.dynamicQueryTimeout = runtime.dynamicQueryTimeout;
    config.dynamicRouteTimeout = runtime.dynamicRouteTimeout;
    config.maxDynamicBodySize = runtime.maxDynamicBodySize;
    config.maxConcurrentDbOperations = runtime.maxConcurrentDbOperations;
    config.preparedDynamicQueries = runtime.preparedDynamicQueries;
#if QORNIX_ENABLE_ASYNC_DB
    if (!qornix_dynamic_api::dynamicAsyncDbSupportsPreparedQueries(config.databaseConfig)) {
        config.preparedDynamicQueries = false;
    }
#endif

    server.add_route("/", makeDynamicHomeHandler());
    server.add_route("/schema-manager", makeSchemaManagerPageHandler());
    server.add_route("/query-builder", makeQueryBuilderPageHandler());
    server.add_route("/table", makeTableBrowserPageHandler());
    server.add_route("/table/{table}/{id}", makeRowViewPageHandler());
    server.add_route("/api-playground", makeApiPlaygroundPageHandler());
    server.add_route("/docs", makeDocsPageHandler());
    server.add_route("/docs/query-syntax", makeQuerySyntaxPageHandler());
    server.add_route("/docs/schema", makeSchemaDocsPageHandler());
    server.add_route("/docs/schema-manager", makeSchemaDocsPageHandler());
    server.add_route("/instructions", makeInstructionsPageHandler());
    server.add_route("/static/{filename}", StaticFileHandler::create(dynamic_api_app_paths::staticDir().string()));
    server.add_route("/api/demo/reset", makeDemoResetHandler());
    server.add_route("/api/demo/setup", makeDemoSetupHandler());

#if QORNIX_ENABLE_ASYNC_DB
    qornix_dynamic_api::DynamicAsyncDatabaseOptions asyncOptions;
    asyncOptions.pool.pool_name = "dynamic-api-app";
    asyncOptions.pool.max_connections = 8;
    asyncOptions.pool.max_waiters = 256;
    asyncOptions.pool.acquire_timeout = std::chrono::milliseconds{500};
    asyncOptions.pool.query_timeout = config.dynamicQueryTimeout;
    asyncOptions.offloadedWorkerThreads = 1;

    auto asyncDatabase = qornix_dynamic_api::makeDynamicAsyncDatabaseInterface(
        server.executor(),
        config.databaseConfig,
        asyncOptions);
    auto allowlist = qornix_dynamic_api::loadDynamicAsyncAllowlist(config);
    qornix_dynamic_api::addSchemaDrivenDynamicApiAsyncRoutes(
        server,
        config,
        std::move(asyncDatabase),
        std::move(allowlist));
#else
    qornix_dynamic_api::addSchemaDrivenDynamicApiRoutes(server, config);
#endif
}
