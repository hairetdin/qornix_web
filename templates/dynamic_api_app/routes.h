#pragma once

#include "app_paths.h"
#include "runtime_config.h"
#include "http_server.h"
#include "handler_base.h"
#include "handlers/dynamic_app_page_handlers.h"

#include <qornix_dynamic_api/dynamic_api_config.h>
#include <qornix_dynamic_api/dynamic_api_handlers.h>

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

    qornix_dynamic_api::addSchemaDrivenDynamicApiRoutes(server, config);
}
