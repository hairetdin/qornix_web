/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

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
