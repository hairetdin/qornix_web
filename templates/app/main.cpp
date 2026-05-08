#include "server_manager.h"
#include "routes.h"

#include <iostream>

int main(int argc, char* argv[]) {
    try {
        auto server = create_server_manager(argc, argv);
        server->addRouteFunction(setupRoutes);
        server->run();
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
