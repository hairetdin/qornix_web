#pragma once

#include "app_paths.h"
#include "http_server.h"
#include "handler_base.h"
#include "handlers/dynamic_app_page_handlers.h"

#include <qornix_dynamic_api/dynamic_api_config.h>
#include <qornix_dynamic_api/dynamic_api_handlers.h>

inline void setupRoutes(HttpServer& server) {
    qornix_dynamic_api::DynamicApiConfig config;
    config.databaseConfig.driver = "sqlite";
    config.databaseConfig.dbname = dynamic_api_app_paths::databasePath().string();
    config.uploadedSchemaPath = dynamic_api_app_paths::uploadedSchemaPath().string();
    config.exportedSchemaPath = dynamic_api_app_paths::exportedSchemaPath().string();
    config.historyPath = dynamic_api_app_paths::historyPath().string();
    config.apiPrefix = "/api/dynamic";
    config.schemaPrefix = "/api/dynamic/schema";
    config.defaultLimit = 100;
    config.maxLimit = 1000;
    // Showcase mode: enable raw JOIN clauses so the generated app can demonstrate the full QueryBuilder surface.
    config.allowRawJoin = true;
config.schemaXsdPath = dynamic_api_app_paths::schemaAppXsdPath().string();

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
