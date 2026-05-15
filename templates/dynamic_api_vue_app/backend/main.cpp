#include "app_paths.h"
#include "server_manager.h"
#include "routes.h"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        if (argc > 0) {
            dynamic_api_app_paths::setExecutablePath(argv[0]);
        }

        auto server = create_server_manager(argc, argv);
        server->addRouteFunction(setupRoutes);
        server->run();
    } catch (const std::exception& e) {
        std::cerr << "Qornix dynamic API application error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
