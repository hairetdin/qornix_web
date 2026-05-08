/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <filesystem>
#include <vector>
#include <limits.h>

#include "http_server.h"
#include "handler_base.h"
#include "entity_api_controller.h"
#include "handler_interface.h"
#include "demo_database.h"


class DynamicEntityHandler : public HandlerBase {
private:
    std::shared_ptr<DatabaseInterface> db_;
    std::unique_ptr<EntityAPIController> api_controller_;

    static std::shared_ptr<DatabaseInterface> initDynamicDb() {
        auto db = DatabaseInterface::init(dynamic_entity_server_example::demoDatabaseConfig());
        std::cout << "Successfully initialized demo SQLite connection" << std::endl;
        return db;
    }

    std::shared_ptr<DatabaseInterface>& getDbForCurrentThread() {
        static thread_local std::shared_ptr<DatabaseInterface> db = initDynamicDb();
        return db;
    }

public:
    DynamicEntityHandler() {
        // Each handler instance gets its own connection — SQLite does NOT support
        // concurrent access from multiple threads to the same sqlite3* handle.
        db_ = getDbForCurrentThread();
        api_controller_ = std::make_unique<EntityAPIController>(db_);
    }

protected:
    void handleGet(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        try {
            auto entity_it = path_params.find("entity");
            if (entity_it == path_params.end()) {
                res.result(http::status::not_found);
                res.set(http::field::content_type, "text/plain");
                res.body() = "Entity not found";
                res.prepare_payload();
                return;
            }

            std::string entityName = entity_it->second;
            auto id_it = path_params.find("id");

            APIResult result;

            if (id_it != path_params.end()) {
                // Get a specific record by ID
                std::string entityId = id_it->second;
                std::map<std::string, std::string> params = {{"id", entityId}};

                // Pass query-string parameters as well
                for (const auto &param: url_view.params()) {
                    params[std::string(param.key)] = std::string(param.value);
                }

                result = api_controller_->handleGetRequestWithStatus(entityName, params, true);
            } else {
                // Get a list with filters from query parameters
                std::map<std::string, std::string> queryParams;
                for (const auto &param: url_view.params()) {
                    queryParams[std::string(param.key)] = std::string(param.value);
                }
                result = api_controller_->handleGetRequestWithStatus(entityName, queryParams, false);
            }

            // Set the correct HTTP status code
            res.result(static_cast<http::status>(result.status_code));
            res.set(http::field::content_type, "application/json");
            res.body() = result.json_response;
            res.prepare_payload();
        } catch (const std::exception &e) {
            // Return 500 for unexpected errors
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "application/json");
            res.body() = "{\"error\": \"Internal server error: " + std::string(e.what()) + "\"}";
            res.prepare_payload();
        }
    }

    void handlePost(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        try {
            auto entity_it = path_params.find("entity");
            if (entity_it == path_params.end()) {
                res.result(http::status::not_found);
                res.set(http::field::content_type, "text/plain");
                res.body() = "Entity not found";
                res.prepare_payload();
                return;
            }

            std::string entityName = entity_it->second;

            APIResult result = api_controller_->handlePostRequestWithStatus(entityName, req.body());

            // Set the correct HTTP status code
            res.result(static_cast<http::status>(result.status_code));
            res.set(http::field::content_type, "application/json");
            res.body() = result.json_response;
            res.prepare_payload();
        } catch (const std::exception &e) {
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "application/json");
            res.body() = "{\"error\": \"Internal server error: " + std::string(e.what()) + "\"}";
            res.prepare_payload();
        }
    }

    void handlePut(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        try {
            // Use both path parameters
            auto entity_it = path_params.find("entity");
            auto id_it = path_params.find("id");

            if (entity_it == path_params.end() || id_it == path_params.end()) {
                res.result(http::status::bad_request);
                res.set(http::field::content_type, "application/json");
                res.body() = "{\"error\": \"Entity name and ID required for PUT request\"}";
                res.prepare_payload();
                return;
            }

            std::string entityName = entity_it->second;
            std::string entityId = id_it->second;

            APIResult result = api_controller_->handlePutRequestWithStatus(entityName, entityId, req.body());

            // Set the correct HTTP status code
            res.result(static_cast<http::status>(result.status_code));
            res.set(http::field::content_type, "application/json");
            res.body() = result.json_response;
            res.prepare_payload();
        } catch (const std::exception &e) {
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "application/json");
            res.body() = "{\"error\": \"Internal server error: " + std::string(e.what()) + "\"}";
            res.prepare_payload();
        }
    }

    void handlePatch(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        try {
            auto entity_it = path_params.find("entity");
            auto id_it = path_params.find("id");

            if (entity_it == path_params.end() || id_it == path_params.end()) {
                res.result(http::status::bad_request);
                res.set(http::field::content_type, "application/json");
                res.body() = "{\"error\": \"Entity name and ID required for PATCH request\"}";
                res.prepare_payload();
                return;
            }
            std::string entityName = entity_it->second;
            std::string entityId = id_it->second;

            APIResult result = api_controller_->handlePatchRequestWithStatus(entityName, entityId, req.body());

            // Set the correct HTTP status code
            res.result(static_cast<http::status>(result.status_code));
            res.set(http::field::content_type, "application/json");
            res.body() = result.json_response;
            res.prepare_payload();
        } catch (const std::exception &e) {
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "application/json");
            res.body() = "{\"error\": \"Internal server error: " + std::string(e.what()) + "\"}";
            res.prepare_payload();
        }
    }

    void handleDelete(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        try {
            auto entity_it = path_params.find("entity");
            auto id_it = path_params.find("id");

            if (entity_it == path_params.end() || id_it == path_params.end()) {
                res.result(http::status::bad_request);
                res.set(http::field::content_type, "application/json");
                res.body() = "{\"error\": \"Entity name and ID required for DELETE request\"}";
                res.prepare_payload();
                return;
            }
            std::string entityName = entity_it->second;
            std::string entityId = id_it->second;

            APIResult result = api_controller_->handleDeleteRequestWithStatus(entityName, entityId);

            // Set the correct HTTP status code
            res.result(static_cast<http::status>(result.status_code));
            res.set(http::field::content_type, "application/json");
            res.body() = result.json_response;
            res.prepare_payload();
        } catch (const std::exception &e) {
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "application/json");
            res.body() = "{\"error\": \"Internal server error: " + std::string(e.what()) + "\"}";
            res.prepare_payload();
        }
    }
};

class StaticHandler : public HandlerBase {
private:
    std::string generateApiDocumentation() {
        std::string doc = "Добро пожаловать в Dynamic Entity Server!\n\n";
        doc += "entity - это название таблицы в базе данных\n\n";
        doc += "=== ДОСТУПНЫЕ API ЭНДПОИНТЫ ===\n\n";

        doc += "БАЗОВЫЕ ОПЕРАЦИИ:\n";
        doc += "GET    /api/{entity}          - Получить список всех сущностей\n";
        doc += "GET    /api/{entity}/{id}     - Получить сущность по ID\n";
        doc += "POST   /api/{entity}          - Создать новую сущность\n";
        doc += "PUT    /api/{entity}/{id}     - Полное обновление сущности\n";
        doc += "PATCH  /api/{entity}/{id}     - Частичное обновление сущности\n";
        doc += "DELETE /api/{entity}/{id}     - Удалить сущность\n\n";

        doc += "ПАРАМЕТРЫ ФИЛЬТРАЦИИ (для GET /api/{entity}):\n";
        doc += "- ?field=value               - Фильтр по полю (равенство)\n";
        doc += "- ?field__gte=value          - Больше или равно\n";
        doc += "- ?field__gt=value           - Больше чем\n";
        doc += "- ?field__lte=value          - Меньше или равно\n";
        doc += "- ?field__lt=value           - Меньше чем\n";
        doc += "- ?field__ne=value           - Не равно\n";
        doc += "- ?field__in=value1,value2   - В списке значений\n";
        doc += "- ?field__like=%pattern%     - Поиск по шаблону\n\n";

        doc += "ПАРАМЕТРЫ СОРТИРОВКИ И ОГРАНИЧЕНИЙ:\n";
        doc += "- ?order_by=field            - Сортировка по полю (ASC)\n";
        doc += "- ?order_by=-field           - Сортировка по полю (DESC)\n";
        doc += "- ?limit=N                   - Ограничить количество результатов\n";
        doc += "- ?offset=N                  - Пропустить N записей\n";
        doc += "- ?values=field1,field2      - Выбрать только указанные поля\n\n";

        doc += "ПРИМЕРЫ ИСПОЛЬЗОВАНИЯ:\n\n";

        doc += "1. Получить все пользователи:\n";
        doc += "   GET /api/users\n\n";

        doc += "2. Получить пользователя с ID=1:\n";
        doc += "   GET /api/users/1\n\n";

        doc += "3. Найти пользователей старше 25 лет:\n";
        doc += "   GET /api/users?age__gte=25\n\n";

        doc += "4. Найти пользователей с именем, содержащим 'John':\n";
        doc += "   GET /api/users?name__like=%John%\n\n";

        doc += "5. Получить первые 10 пользователей, отсортированных по имени:\n";
        doc += "   GET /api/users?order_by=name&limit=10\n\n";

        doc += "6. Получить только имя и email пользователей:\n";
        doc += "   GET /api/users?values=name,email\n\n";

        doc += "7. Создать нового пользователя:\n";
        doc += "   POST /api/users\n";
        doc += "   Content-Type: application/json\n";
        doc += "   {\n";
        doc += "     \"name\": \"John Doe\",\n";
        doc += "     \"email\": \"john@example.com\",\n";
        doc += "     \"age\": 30\n";
        doc += "   }\n\n";

        doc += "8. Обновить пользователя полностью:\n";
        doc += "   PUT /api/users/1\n";
        doc += "   Content-Type: application/json\n";
        doc += "   {\n";
        doc += "     \"name\": \"John Smith\",\n";
        doc += "     \"email\": \"johnsmith@example.com\",\n";
        doc += "     \"age\": 31\n";
        doc += "   }\n\n";

        doc += "9. Частично обновить email пользователя:\n";
        doc += "   PATCH /api/users/1\n";
        doc += "   Content-Type: application/json\n";
        doc += "   {\n";
        doc += "     \"email\": \"newemail@example.com\"\n";
        doc += "   }\n\n";

        doc += "10. Удалить пользователя:\n";
        doc += "    DELETE /api/users/1\n\n";

        doc += "ФОРМАТ ОТВЕТА:\n";
        doc += "Успешные запросы возвращают JSON массив объектов.\n";
        doc += "Ошибки возвращаются в формате: {\"error\": \"сообщение об ошибке\"}\n\n";

        doc += "СТАТУС КОДЫ:\n";
        doc += "- 200 OK          - Успешный запрос\n";
        doc += "- 201 Created     - Ресурс успешно создан\n";
        doc += "- 400 Bad Request - Некорректный запрос или ошибка валидации\n";
        doc += "- 404 Not Found   - Ресурс не найден (таблица не существует или запись не найдена)\n";
        doc += "- 500 Internal Server Error - Внутренняя ошибка сервера\n";
        doc += "- 503 Service Unavailable - Ошибка подключения к базе данных";

        return doc;
    }

public:
    StaticHandler() {
    } // Default constructor

protected:
    void handleGet(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/plain; charset=utf-8");
        res.body() = generateApiDocumentation();
        res.prepare_payload();
    }
};

int main() {
    try {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cout << "=== Dynamic Entity Server Starting ===" << std::endl;
            std::cout << "Executable path: " << cwd << std::endl;
        }

        // Initialize the demo SQLite database
        dynamic_entity_server_example::ensureDemoDatabase();

        net::io_context ioc;
        tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), 8008);

        HttpServer server(ioc, endpoint);

        // Register the handler for API routes
        auto handler = std::make_shared<DynamicEntityHandler>();
        server.add_route("/api/{entity}", handler);
        server.add_route("/api/{entity}/{id}", handler);

        // Add a simple handler for the root path
        server.add_route("/", std::make_shared<StaticHandler>());

        std::cout << "Server started on 127.0.0.1:8008" << std::endl;
        std::cout << "API endpoints:" << std::endl;
        std::cout << "  GET    /api/{entity}          - List all entities" << std::endl;
        std::cout << "  GET    /api/{entity}/{id}     - Get entity by ID" << std::endl;
        std::cout << "  POST   /api/{entity}          - Create new entity" << std::endl;
        std::cout << "  PUT    /api/{entity}/{id}     - Update entity" << std::endl;
        std::cout << "  PATCH  /api/{entity}/{id}     - Partial update entity" << std::endl;
        std::cout << "  DELETE /api/{entity}/{id}     - Delete entity" << std::endl;
        std::cout << "\nEnhanced error handling with proper HTTP status codes:" << std::endl;
        std::cout << "  - 404: Entity/table not found" << std::endl;
        std::cout << "  - 400: Validation errors" << std::endl;
        std::cout << "  - 500: Internal server errors" << std::endl;
        std::cout << "  - 503: Database connection errors" << std::endl;

        server.run();

        ioc.run();
    } catch (const std::exception &e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
