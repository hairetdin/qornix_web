/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "server_manager.h"
#include "routes.h"
#include <iostream>
#include <unistd.h>
#include <linux/limits.h>

int main(int argc, char* argv[]) {
    try {
        // Print startup information
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cout << "=== Dynamic Web Query Builder Server Starting ===" << std::endl;
            std::cout << "Executable path: " << cwd << std::endl;
        }

        // Create and start the server through ServerManager
        auto server_manager = create_server_manager(argc, argv);

        // Set the custom route setup function from the current project
        server_manager->addRouteFunction(setupDynamicRoutes);

        // Print route information
        std::cout << "\nExtract Method… Available endpoints:" << std::endl;
        std::cout << "  GET    /                      - API Documentation (this page)" << std::endl;
        std::cout << "  GET    /query-builder         - Interactive Query Builder Interface" << std::endl;
        std::cout << "  GET    /schema-manager        - XML schema import/export page" << std::endl;
        std::cout << "  GET    /api/dynamic/meta/tables?q=us - Tables list (autocomplete by prefix)" << std::endl;
        std::cout << "  GET    /api/dynamic/meta/{table}/fields?q=id - Table fields (autocomplete by prefix)" << std::endl;
        std::cout << "  GET    /api/dynamic/meta/autocomplete?type=tables|fields&table={table}&q=... - Universal autocomplete" << std::endl;
        std::cout << "  GET    /api/dynamic/schema.xml - Export current demo SQLite schema as XML" << std::endl;
        std::cout << "  POST   /api/dynamic/schema/apply - Apply XML schema to current demo SQLite database" << std::endl;
        std::cout << "  GET    /api/dynamic/{table}   - List all records from table" << std::endl;
        std::cout << "  POST   /api/dynamic/{table}   - Create new record in table" << std::endl;
        std::cout << "  GET    /api/dynamic/{table}/{id} - Get specific record" << std::endl;
        std::cout << "  PUT    /api/dynamic/{table}/{id} - Update specific record" << std::endl;
        std::cout << "  DELETE /api/dynamic/{table}/{id} - Delete specific record" << std::endl;
        std::cout << "\n🔧 Enhanced error handling with proper HTTP status codes:" << std::endl;
        std::cout << "  - 404: Table/entity not found" << std::endl;
        std::cout << "  - 400: Validation errors" << std::endl;
        std::cout << "  - 500: Internal server errors" << std::endl;
        std::cout << "  - 503: Database connection errors" << std::endl;
        std::cout << "\n📊 Server configuration managed by ServerManager" << std::endl;
        std::cout << "Use --help flag to see available command line options" << std::endl;

        // Start the server
        server_manager->run();

    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
