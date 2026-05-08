/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

// main.cpp
#include "http_server.h"
#include "static_routes.h"
#include <boost/asio.hpp>
#include <iostream>

int main() {
    try {
        net::io_context ioc;
        tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), 8008);

        HttpServer server(ioc, endpoint);

        // Configure routes for static pages
        setupStaticPagesRoutes(server);

        std::cout << "Сервер запущен на 127.0.0.1:8008" << std::endl;
        std::cout << "Откройте в браузере:" << std::endl;
        std::cout << "  http://127.0.0.1:8008/ - Главная страница" << std::endl;
        std::cout << "  http://127.0.0.1:8008/about - Страница О нас" << std::endl;

        server.run();

        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
