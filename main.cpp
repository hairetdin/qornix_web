/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "server_manager.h"
#include <iostream>

#include "routes.h"

int main(int argc, char* argv[]) {
    try {
        auto server_manager = create_server_manager(argc, argv);
        server_manager->addRouteFunction(anotherSetupRoutes);
        server_manager->run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
