#include "server_manager.h"
#include "routes.h"
#include <iostream>
int main(int argc, char* argv[]) {
    try {
        auto server = create_server_manager(argc, argv);
        server->addRouteFunction(setupRoutes);
        std::cout << "Schema Manager: http://127.0.0.1:8008/schema-manager" << std::endl;
        server->run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}
