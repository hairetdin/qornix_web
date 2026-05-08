// static_routes.h
#include "http_server.h"
#include "static_pages_handlers.h"
#include <memory>

void setupStaticPagesRoutes(HttpServer& srv) {
    auto homeHandler = std::make_shared<HomePageHandler>();
    auto aboutHandler = std::make_shared<AboutPageHandler>();

    srv.add_route("/", homeHandler);
    srv.add_route("/about", aboutHandler);
}
