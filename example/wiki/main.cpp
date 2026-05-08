/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "http_server.h"
#include "wiki_routes.h"
#include "wiki_handlers.h"
#include "../include/log_initializer.h"

#include <boost/asio.hpp>
#include <iostream>
#include <string>

namespace {
unsigned short parsePort(int argc, char* argv[], unsigned short defaultPort) {
    unsigned short port = defaultPort;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            try {
                const int parsed = std::stoi(argv[++i]);
                if (parsed > 0 && parsed <= 65535) {
                    port = static_cast<unsigned short>(parsed);
                }
            } catch (...) {
                std::cerr << "Invalid port value, using default " << defaultPort << std::endl;
            }
        }
    }
    return port;
}
}

int main(int argc, char* argv[]) {
    try {
        // Load configuration from config.yaml
        auto config = wiki_example::loadWikiConfig("config.yaml");

        // Override port from CLI if specified
        const auto port = parsePort(argc, argv, config.server_port);

        // Initialize logging from config
        LogInitializer::initConsoleLogging();
        if (config.logging_to_file) {
            LogInitializer::initFileLogging(
                config.logging_file_path,
                config.logging_rotation_size,
                config.logging_max_files
            );
        }
        LogInitializer::setLogLevel(wiki_example::toBoostLogLevel(wiki_example::stringToLogLevel(config.logging_level)));

        net::io_context ioc;
        tcp::endpoint endpoint(net::ip::make_address(config.server_address), port);

        HttpServer server(ioc, endpoint);
        setupWikiRoutes(server, config);

        std::cout << "Wiki server started" << std::endl;
        std::cout << "  UI: http://" << config.server_address << ":" << port << "/" << std::endl;
        std::cout << "  API: http://" << config.server_address << ":" << port << "/api/wiki/articles" << std::endl;
        std::cout << "  SQLite: " << wiki_example::databasePath(config) << std::endl;

        server.run();
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Wiki server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
