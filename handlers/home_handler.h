/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "handler_base.h"
#include "template_loader.h"
#include <boost/url.hpp>
#include <map>

namespace http = boost::beast::http;
namespace urls = boost::urls;

class HomeHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        std::string path = std::string(url_view.path());

        if (path == "/") {
            // Load the home page from the template
            std::string htmlContent;

            // Check for a custom home.html template
            if (TemplateLoader::templateExists("home.html")) {
                htmlContent = TemplateLoader::loadFile("home.html");
            } else {
                // Use the built-in template if the file is not found
                htmlContent = getDefaultHomePage();
            }

            buildHtmlResponse(res, http::status::ok, htmlContent);
        } else if (path == "/health") {
            buildJsonResponse(res, http::status::ok, R"({"status": "healthy", "message": "Server is running"})");
        } else if (path == "/info") {
            buildJsonResponse(res, http::status::ok, R"({
                "server": "Qornix Web Server",
                "version": "0.0.1",
                "framework": "Qornix",
                "language": "C++17",
                "features": ["DI Container", "Middleware", "Dynamic Extensions"]
            })", "application/json; charset=utf-8");
        } else {
            // Use the 404 template for nonexistent pages
            std::string notFoundContent = TemplateLoader::load404Template();
            buildHtmlResponse(res, http::status::not_found, notFoundContent);
        }
    }

private:
    std::string getDefaultHomePage() {
        return R"(
        <!DOCTYPE html>
        <html lang="ru">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Qornix Web Server</title>
            <link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E%3Ctext y='.9em' font-size='90' font-family='Arial, sans-serif'%3E🦅%3C/text%3E%3C/svg%3E">
            <style>
                body { font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }
                .container { max-width: 800px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 0 20px rgba(0,0,0,0.1); }
                h1 { color: #2c3e50; text-align: center; }
                .features { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 30px 0; }
                .feature { background: #ecf0f1; padding: 20px; border-radius: 8px; }
                .feature h3 { color: #3498db; margin-top: 0; }
                .routes { background: #fff; border-left: 4px solid #3498db; padding: 15px; margin: 15px 0; }
                code { background: #f8f9fa; padding: 2px 6px; border-radius: 4px; font-family: monospace; }
                .btn { display: inline-block; background: #3498db; color: white; padding: 10px 20px; text-decoration: none; border-radius: 5px; margin: 5px; }
            </style>
        </head>
        <body>
            <div class="container">
                <h1>🦅 Qornix Web Server</h1>
                <div style="margin-top: 30px; padding: 20px; background: #f8f9fa; border-radius: 8px;">
                    <p>QORNIX - Query Object Routing Network Integration eXperience</p>
                </div>
                <p>Добро пожаловать в расширяемый веб-сервер на C++! Этот сервер построен на архитектуре, которая позволяет легко добавлять новую функциональность.</p>
                <p>Сервер построен на технологии Boost.Beast и поддерживает современные веб-стандарты.</p>

                <div class="features">
                    <div class="feature">
                        <h3>🔌 Расширяемость</h3>
                        <p>Добавляйте новые маршруты и функциональность через динамически загружаемые расширения</p>
                    </div>
                    <div class="feature">
                        <h3>🔄 Dependency Injection</h3>
                        <p>Управление зависимостями через контейнер с поддержкой различных сроков жизни объектов</p>
                    </div>
                    <div class="feature">
                        <h3>🧩 Middleware</h3>
                        <p>Цепочка middleware для логирования, аутентификации и других задач</p>
                    </div>
                    <div class="feature">
                        <h3>⚡ Асинхронность</h3>
                        <p>Многопоточная обработка запросов для высокой производительности</p>
                    </div>
                </div>

                <div class="routes">
                    <h3>🧭 Доступные маршруты:</h3>
                    <ul>
                        <li><code>/</code> - эта страница</li>
                        <li><code>GET /health</code> - <a href="/health" class="btn">Проверить состояние сервера</a></li>
                        <li><code>GET /info</code> - <a href="/info" class="btn">Информация о сервере</a></li>
                        <li><code>GET /notfound</code> - <a href="/notfound" class="btn">Страница 404</a></li>
                        <li><code>/users</code> - управление пользователями</li>
                        <li><code>/users/{id}</code> - <a href="/users/1" class="btn">доступ к конкретному пользователю</a></li>
                        <li><code>/api/v1/users</code> - API для работы с пользователями</li>
                        <li><code>/api/v1/users/{id}</code> - API для работы с конкретным пользователем</li>
                    </ul>
                </div>

                <div style="margin-top: 30px; padding: 20px; background: #f8f9fa; border-radius: 8px;">
                    <h3>🛠️ Для разработчиков:</h3>
                    <p>Добавляйте новые маршруты через server_manager->addRouteFunction(anotherRouteFunction);</p>
                    <p>Создавайте свои расширения в папке <code>route_extensions/</code></p>
                    <p>Используйте <code>BaseHandler</code> как основу для своих обработчиков</p>
                    <p>Регистрируйте сервисы через <code>DIContainer</code></p>
                </div>
            </div>
        </body>
        </html>
        )";
    }
};
