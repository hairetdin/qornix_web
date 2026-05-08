/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <memory>
#include <functional>
#include <vector>

#include "http_server.h"

class ServerManager {
public:
    ServerManager(int argc, char* argv[]);
    ~ServerManager() = default;

    // Method for setting the custom route function
    void addRouteFunction(std::function<void(HttpServer&)> route_func) {
        custom_routes_.push_back(route_func);
    }

    void run();

private:
    void parse_arguments(int argc, char* argv[]);
    void setup_server();
    void setup_logging();
    void setup_routes();
    void setup_middleware();
    void setup_di_container();
    void start_io_threads();
    std::vector<std::function<void(HttpServer&)>> custom_routes_;
};

std::unique_ptr<ServerManager> create_server_manager(int argc, char* argv[]);
