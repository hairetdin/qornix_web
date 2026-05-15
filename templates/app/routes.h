#pragma once

#include "app_paths.h"
#include "http_server.h"
#include "handlers/health_handler.h"
#include "handlers/page_handlers.h"
#include "handlers/project_structure_handlers.h"
#include "handlers/async_handlers.h"

#include <memory>
#include <string>

inline void setupRoutes(HttpServer& server) {
    auto healthHandler = std::make_shared<HealthHandler>();

    server.add_route("/", makeHomePageHandler());
    server.add_route("/health", healthHandler);

    server.get_async("/async/ping", makeAsyncPingHandler());
    server.get_async("/async/sleep/{ms}", makeAsyncSleepHandler());
    server.add_route("/project-structure", makeProjectStructurePageHandler());
    server.add_route("/api/project/structure", makeProjectStructureListHandler());
    server.add_route("/api/project/archive", makeProjectArchiveHandler());

    server.add_route("/docs", makeDocsPageHandler());
    server.add_route("/docs/getting-started", makeGettingStartedPageHandler());
    server.add_route("/docs/routing", makeRoutingDocsPageHandler());
    server.add_route("/docs/configuration", makeConfigurationDocsPageHandler());
    server.add_route("/docs/deployment", makeDeploymentDocsPageHandler());
    server.add_route("/static/{filename}", StaticFileHandler::create(qornix_app_paths::staticDir().string()));
    server.add_route("/docs/raw/{filename}", makeMarkdownDocHandler());
}
