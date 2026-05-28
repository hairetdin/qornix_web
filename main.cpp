/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "server_manager.h"
#include <iostream>

#include "routes.h"

#ifdef QORNIX_BUILD_RAG
#include "rag_extension.h"
#include "config_parser.h"
#endif

int main(int argc, char* argv[]) {
    try {
        auto server_manager = create_server_manager(argc, argv);
        server_manager->addRouteFunction(anotherSetupRoutes);

#ifdef QORNIX_BUILD_RAG
        // Optional RAG Integration
        auto config = server_manager->getConfig();

        if (config.count("rag.enabled") && config["rag.enabled"] == "true") {
            std::cout << "\n🚀 Initializing RAG module..." << std::endl;

            auto ragExtension = std::make_shared<RagExtension>();

            // Configure RAG from flattened config map
            std::map<std::string, std::string> rag_config;
            for (const auto& [key, value] : config) {
                if (key.find("rag.") == 0) {  // Keys starting with "rag."
                    rag_config[key.substr(4)] = value;  // Remove "rag." prefix
                }
            }

            ragExtension->configure(rag_config);
            ragExtension->initialize(server_manager->getDIContainer());

            // Register RAG routes
            server_manager->addRouteFunction([ragExtension, &server_manager](HttpServer& srv) {
                ragExtension->registerRoutes(srv, server_manager->getDIContainer());
            });

            std::cout << "✅ RAG module initialized and routes registered\n" << std::endl;
        } else {
            std::cout << "\nℹ️  RAG module disabled (set rag.enabled=true in config.yaml)\n" << std::endl;
        }
#endif

        server_manager->run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
