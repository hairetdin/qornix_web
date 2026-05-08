/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "database/query_builder.h"
#include "database/database_interface.h"
#include "test_utils.h"

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

struct TestError {
    std::string testName;
    std::string errorMessage;
};

class QueryBuilderTest {
private:
    std::unique_ptr<QueryBuilder> builder;
    std::unique_ptr<qornix_orm_tests::TempSqliteConfig> sqliteConfig;
    int totalErrors = 0;
    int totalTestsRun = 0;
    std::vector<TestError> errorDetails;
    std::string driverType; // Type driver (postgresql or sqlite)

    void reportError(const std::string &testName, const std::string &message) {
        TestError error;
        error.testName = testName;
        error.errorMessage = message;
        errorDetails.push_back(error);
        totalErrors++;
    }

    void createTestTables() {
        try {
            // Determine Type driver from configuration
            auto db_config = builder->db_interface->getDatabaseConfig();
            driverType = db_config.driver;

            if (driverType == "sqlite" || driverType == "sqlite3") {
                // SQLite versiya
                builder->executeNonQuery("DROP TABLE IF EXISTS users");
                builder->executeNonQuery("DROP TABLE IF EXISTS departments");

                std::string createDepartmentsQuery = R"(
                    CREATE TABLE departments (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        name TEXT NOT NULL
                    )
                )";
                builder->executeNonQuery(createDepartmentsQuery);

                std::string createUsersQuery = R"(
                    CREATE TABLE users (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        name TEXT NOT NULL,
                        email TEXT UNIQUE,
                        age INTEGER,
                        status TEXT DEFAULT 'active',
                        department_id INTEGER REFERENCES departments(id)
                    )
                )";
                builder->executeNonQuery(createUsersQuery);

                std::string insertDepartmentsQuery = R"(
                    INSERT INTO departments (name) VALUES
                    ('IT'),
                    ('HR'),
                    ('Finance')
                )";
                builder->executeNonQuery(insertDepartmentsQuery);

                std::string insertUsersQuery = R"(
                    INSERT INTO users (name, email, age, status, department_id) VALUES
                    ('Иван', 'ivan@example.com', 25, 'active', 1),
                    ('Петр', 'petr@example.com', 30, 'inactive', 2),
                    ('Мария', 'maria@example.com', 22, 'active', 3)
                )";
                builder->executeNonQuery(insertUsersQuery);

            } else {
                // PostgreSQL versiya
                builder->executeNonQuery("DROP TABLE IF EXISTS users CASCADE");
                builder->executeNonQuery("DROP TABLE IF EXISTS departments CASCADE");

                std::string createDepartmentsQuery = R"(
                    CREATE TABLE departments (
                        id SERIAL PRIMARY KEY,
                        name VARCHAR(100) NOT NULL
                    )
                )";
                builder->executeNonQuery(createDepartmentsQuery);

                std::string createUsersQuery = R"(
                    CREATE TABLE users (
                        id SERIAL PRIMARY KEY,
                        name VARCHAR(100) NOT NULL,
                        email VARCHAR(100) UNIQUE,
                        age INTEGER,
                        status VARCHAR(20) DEFAULT 'active',
                        department_id INTEGER REFERENCES departments(id)
                    )
                )";
                builder->executeNonQuery(createUsersQuery);

                std::string insertDepartmentsQuery = R"(
                    INSERT INTO departments (name) VALUES
                    ('IT'),
                    ('HR'),
                    ('Finance')
                )";
                builder->executeNonQuery(insertDepartmentsQuery);

                std::string insertUsersQuery = R"(
                    INSERT INTO users (name, email, age, status, department_id) VALUES
                    ('Иван', 'ivan@example.com', 25, 'active', 1),
                    ('Петр', 'petr@example.com', 30, 'inactive', 2),
                    ('Мария', 'maria@example.com', 22, 'active', 3)
                )";
                builder->executeNonQuery(insertUsersQuery);
            }

            std::cout << "✓ Тестовые таблицы созданы успешно (драйвер: " << driverType << ")" << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "✗ Ошибка при создании тестовых таблиц: " << e.what() << std::endl;
            throw;
        }
    }


    void dropTestTables() {
        try {
            if (driverType == "sqlite" || driverType == "sqlite3") {
                builder->executeNonQuery("DROP TABLE IF EXISTS users");
                builder->executeNonQuery("DROP TABLE IF EXISTS departments");
            } else {
                builder->executeNonQuery("DROP TABLE IF EXISTS users CASCADE");
                builder->executeNonQuery("DROP TABLE IF EXISTS departments CASCADE");
            }

            std::cout << "✓ Тестовые таблицы удалены" << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "✗ Ошибка при удалении тестовых таблиц: " << e.what() << std::endl;
        }
    }

public:
    void setUp() {
        try {
            sqliteConfig = std::make_unique<qornix_orm_tests::TempSqliteConfig>("query_builder_test");
            builder = QueryBuilder::init(sqliteConfig->configPath());

            auto db_config = builder->db_interface->getDatabaseConfig();
            driverType = db_config.driver;

            std::cout << "✓ QueryBuilder инициализирован успешно (драйвер: " << driverType << ")" << std::endl;

            createTestTables();
        } catch (const std::exception &e) {
            std::cerr << "✗ Ошибка инициализации QueryBuilder: " << e.what() << std::endl;
            throw;
        }
    }

    void tearDown() {
        dropTestTables();
        builder.reset();
        sqliteConfig.reset();
    }

    bool runAllTests() {
        setUp();

        testSchemaMetadataMethods();
        testParseJsonRequest();
        testGenerateSQL();
        testExecuteQuery();
        testPostRequest();
        testPatchRequest();
        testDeleteRequest();
        testParseFromJson();
        testSqlInjectionProtection();
        testGetResponse();
        testGetJsonResponse();
        testParseUrlQuery();

        tearDown();

        if (totalErrors == 0) {
            std::cout << "✓ Все " << totalTestsRun << " тестов пройдены успешно!" << std::endl;
        } else {
            std::cout << "\n❌ Ошибки: " << totalErrors << " из " << totalTestsRun << " тестов завершились с ошибками!\n"
                    << std::endl;

            std::cout << "=== Детали ошибок ===" << std::endl;
            for (const auto &error: errorDetails) {
                std::cout << "- [" << error.testName << "] " << error.errorMessage << std::endl;
            }
        }

        return totalErrors == 0;
    }

    void testSchemaMetadataMethods() {
        std::cout << "Тестирование методов метаданных схемы (таблицы/поля)..." << std::endl;
        std::string currentTestName = "testSchemaMetadataMethods";
        totalTestsRun++;

        try {
            auto tableNames = builder->db_interface->getTableNames();
            bool hasUsers = std::find(tableNames.begin(), tableNames.end(), "users") != tableNames.end();
            bool hasDepartments = std::find(tableNames.begin(), tableNames.end(), "departments") != tableNames.end();

            if (!hasUsers || !hasDepartments) {
                std::string errorMsg = "Список таблиц не содержит users/departments";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
                return;
            }

            auto userFields = builder->db_interface->getTableFields("users");
            bool hasId = std::find(userFields.begin(), userFields.end(), "id") != userFields.end();
            bool hasName = std::find(userFields.begin(), userFields.end(), "name") != userFields.end();
            bool hasEmail = std::find(userFields.begin(), userFields.end(), "email") != userFields.end();

            if (!hasId || !hasName || !hasEmail) {
                std::string errorMsg = "Список полей users не содержит id/name/email";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
                return;
            }

            std::cout << "  ✓ Методы getTableNames/getTableFields работают корректно" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  ✗ Ошибка при проверке метаданных схемы: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testParseJsonRequest() {
        std::cout << "Тестирование парсинга JSON-запроса..." << std::endl;
        std::string currentTestName = "testParseJsonRequest";
        totalTestsRun++;

        std::string json_request = R"({
            "method": "GET",
            "table": "users",
            "query": {
                "filter": ["status='active'", "age__gt=18"],
                "order_by": ["name ASC"],
                "limit": 10
            },
            "data": {
                "name": "John Doe",
                "email": "john@example.com",
                "age": 25
            }
        })";

        try {
            builder->parseRequest(json_request);

            if (builder->request_data.method == HttpMethod::GET &&
                builder->request_data.table == "users" &&
                builder->request_data.filters.size() == 2 &&
                builder->request_data.orders.size() == 1 &&
                builder->request_data.limit_value == 10 &&
                builder->request_data.data.contains("name") &&
                builder->request_data.data.contains("email") &&
                builder->request_data.data.contains("age")) {
                std::cout << "  ✓ Парсинг JSON-запроса работает корректно" << std::endl;
            } else {
                std::string errorMsg = "Парсинг JSON-запроса не работает корректно";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при парсинге JSON-запроса: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testGenerateSQL() {
        std::cout << "Тестирование генерации SQL-запроса..." << std::endl;
        std::string currentTestName = "testGenerateSQL";
        totalTestsRun++;

        try {
            builder->clearRequestData();
            builder->setMethod("GET")
                    .setTable("users")
                    .addFilter("status='active'")
                    .addFilter("age__gt=18")
                    .addOrderBy("name ASC")
                    .setLimit(10);

            std::string sql = builder->generateSQL();

            std::string expected_sql = "SELECT * FROM users WHERE status=$1 AND age__gt=18 ORDER BY name ASC LIMIT 10";
            if (sql == expected_sql) {
                std::cout << "  ✓ Генерация SQL-запроса работает корректно" << std::endl;
            } else {
                std::string errorMsg = "Генерация SQL-запроса не работает корректно. Ожидалось: " + expected_sql +
                                       ", получено: " + sql;
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при генерации SQL-запроса: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testExecuteQuery() {
        std::cout << "Тестирование выполнения запроса..." << std::endl;
        std::string currentTestName = "testExecuteQuery";
        totalTestsRun++;

        try {
            builder->clearRequestData();
            builder->setMethod("GET")
                    .setTable("users")
                    .addFilter("status='active'")
                    .addOrderBy("name ASC")
                    .setLimit(10);

            auto results = builder->execute();

            if (!results.empty()) {
                std::cout << "  ✓ Выполнение запроса работает корректно" << std::endl;
                for (const auto &row: results) {
                    for (const auto &[key, value]: row) {
                        std::cout << "    " << key << ": " << value << std::endl;
                    }
                }
            } else {
                std::string errorMsg = "Выполнение запроса вернуло пустой результат";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при выполнении запроса: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testPostRequest() {
        std::cout << "Тестирование POST-запроса..." << std::endl;
        std::string currentTestName = "testPostRequest";
        totalTestsRun++;

        try {
            builder->clearRequestData();
            boost::json::object post_data;
            post_data["name"] = "Иван";
            post_data["email"] = "ivan@example.com";
            post_data["age"] = "30";

            builder->setMethod("POST")
                    .setTable("users")
                    .setData(post_data);
            builder->printRawData();

            std::string sql = builder->generateSQL();

            // For SQLite Use RETURNING if podderzhivaetsya
            std::string expected_sql;
            if (driverType == "sqlite" || driverType == "sqlite3") {
                expected_sql = "INSERT INTO users (name, email, age) VALUES ($1, $2, $3) RETURNING id";
            } else {
                expected_sql = "INSERT INTO users (name, email, age) VALUES ($1, $2, $3) RETURNING id";
            }

            if (sql == expected_sql) {
                std::cout << "  ✓ POST-запрос работает корректно" << std::endl;
            } else {
                std::string errorMsg = "POST-запрос не работает корректно. Ожидалось: " + expected_sql + ", получено: "
                                       + sql;
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }

            auto params = builder->getSqlParameters();
            if (params.size() == 3 &&
                params[0] == "Иван" &&
                params[1] == "ivan@example.com" &&
                params[2] == "30") {
                std::cout << "  ✓ Параметры запроса корректны" << std::endl;
            } else {
                std::string errorMsg =
                        "Параметры запроса некорректны. Ожидалось: ['Иван', 'ivan@example.com', '30'], получено: [";
                for (const auto &p: params) {
                    errorMsg += p + ", ";
                }
                errorMsg += "]";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при тестировании POST-запроса: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testPatchRequest() {
        std::cout << "Тестирование PATCH-запроса..." << std::endl;
        std::string currentTestName = "testPatchRequest";
        totalTestsRun++;

        try {
            builder->clearRequestData();
            boost::json::object patch_data;
            patch_data["name"] = "Иван Петров";
            patch_data["age"] = "31";

            builder->setMethod("PATCH")
                    .setTable("users")
                    .setData(patch_data)
                    .addFilter("id=123");

            std::string sql = builder->generateSQL();

            std::string expected_sql = "UPDATE users SET name = $1, age = $2 WHERE id=123";
            if (sql == expected_sql) {
                std::cout << "  ✓ PATCH-запрос работает корректно" << std::endl;
            } else {
                std::string errorMsg = "PATCH-запрос не работает корректно. Ожидалось: " + expected_sql + ", получено: "
                                       + sql;
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }

            auto params = builder->getSqlParameters();
            if (params.size() == 2 &&
                params[0] == "Иван Петров" &&
                params[1] == "31") {
                std::cout << "  ✓ Параметры запроса корректны" << std::endl;
            } else {
                std::string errorMsg = "Параметры запроса некорректны. Ожидалось: ['Иван Петров', '31'], получено: [";
                for (const auto &p: params) {
                    errorMsg += p + ", ";
                }
                errorMsg += "]";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при тестировании PATCH-запроса: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testDeleteRequest() {
        std::cout << "Тестирование DELETE-запроса..." << std::endl;
        std::string currentTestName = "testDeleteRequest";
        totalTestsRun++;

        try {
            builder->clearRequestData();
            builder->setMethod("DELETE")
                    .setTable("users")
                    .addFilter("id=123");

            std::string sql = builder->generateSQL();

            std::string expected_sql = "DELETE FROM users WHERE id=123";
            if (sql == expected_sql) {
                std::cout << "  ✓ DELETE-запрос работает корректно" << std::endl;
            } else {
                std::string errorMsg = "DELETE-запрос не работает корректно. Ожидалось: " + expected_sql +
                                       ", получено: " + sql;
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при тестировании DELETE-запроса: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testParseFromJson() {
        std::cout << "Тестирование метода parseJsonQuery с JOIN..." << std::endl;
        std::string currentTestName = "testParseFromJson";
        totalTestsRun++;

        try {
            builder->clearRequestData();

            std::string json_request = R"({
            "method": "GET",
            "table": "users",
            "query": {
                "join": ["JOIN departments ON users.department_id = departments.id"],
                "where": ["departments.name='IT'"],
                "order_by": ["users.name ASC"],
                "limit": 5
            }
        })";

            builder->parseRequest(json_request);

            if (builder->request_data.method == HttpMethod::GET &&
                builder->request_data.table == "users" &&
                builder->request_data.joins.size() == 1 &&
                builder->request_data.filters.size() == 1 &&
                builder->request_data.orders.size() == 1 &&
                builder->request_data.limit_value == 5) {
                std::cout << "  ✓ Метод parseJsonQuery работает корректно" << std::endl;

                auto sql = builder->generateSQL();
                std::cout << "  ✓ SQL-запрос: " << sql << std::endl;

                auto results = builder->execute();

                if (!results.empty()) {
                    std::cout << "  ✓ Запрос успешно выполнен. Получено записей: " << results.size() << std::endl;

                    for (size_t i = 0; i < std::min(results.size(), size_t(3)); ++i) {
                        std::cout << "    Запись " << i + 1 << ": ";
                        for (const auto &[key, value]: results[i]) {
                            std::cout << key << "=" << value << " ";
                        }
                        std::cout << std::endl;
                    }
                } else {
                    std::string errorMsg = "Запрос вернул пустой результат";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            } else {
                std::string errorMsg = "Метод parseJsonQuery не работает корректно";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при тестировании parseJsonQuery: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testSqlInjectionProtection() {
        std::cout << "Тестирование защиты от SQL-инъекций..." << std::endl;
        std::string currentTestName = "testSqlInjectionProtection";
        totalTestsRun++;

        try {
            builder->clearRequestData();

            std::string malicious_table = "users; DROP TABLE users; --";
            builder->setMethod("GET").setTable(malicious_table);

            try {
                builder->generateSQL();
                std::string errorMsg = "Защита от SQL-инъекций не сработала: имя таблицы не было заблокировано";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            } catch (const std::exception &e) {
                std::cout << "  ✓ Защита от SQL-инъекций сработала для имени таблицы: " << e.what() << std::endl;
            }

            builder->clearRequestData();
            std::string malicious_field = "name; DROP TABLE users; --";
            builder->setMethod("GET").setTable("users").addValue(malicious_field);

            try {
                builder->generateSQL();
                std::string errorMsg = "Защита от SQL-инъекций не сработала: имя поля не было заблокировано";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            } catch (const std::exception &e) {
                std::cout << "  ✓ Защита от SQL-инъекций сработала для имени поля: " << e.what() << std::endl;
            }

            builder->clearRequestData();
            std::string malicious_filter = "id = 1; DROP TABLE users; --";
            builder->setMethod("GET").setTable("users").addFilter(malicious_filter);

            try {
                builder->generateSQL();
                std::string errorMsg = "Защита от SQL-инъекций не сработала: фильтр не был заблокирован";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            } catch (const std::exception &e) {
                std::cout << "  ✓ Защита от SQL-инъекций сработала для фильтра: " << e.what() << std::endl;
            }

            builder->clearRequestData();
            std::string malicious_join =
                    "JOIN departments ON users.department_id = departments.id; DROP TABLE users; --";
            builder->setMethod("GET").setTable("users").addJoin(malicious_join);

            try {
                builder->generateSQL();
                std::string errorMsg = "Защита от SQL-инъекций не сработала: JOIN не был заблокирован";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            } catch (const std::exception &e) {
                std::cout << "  ✓ Защита от SQL-инъекций сработала для JOIN: " << e.what() << std::endl;
            }

            std::cout << "  ✓ Все проверки защиты от SQL-инъекций пройдены успешно" << std::endl;
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при тестировании защиты от SQL-инъекций: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testGetResponse() {
        std::cout << "Тестирование метода getResponse..." << std::endl;
        std::string currentTestName = "testGetResponse";
        totalTestsRun++;

        try {
            builder->clearRequestData();
            builder->setMethod("GET")
                    .setTable("users")
                    .addFilter("status='active'")
                    .addOrderBy("name ASC")
                    .setLimit(10);

            ResponseData response = builder->getResponse();

            if (response.status == boost::beast::http::status::ok && response.count > 0) {
                std::cout << "  ✓ Метод getResponse работает корректно" << std::endl;
            } else {
                std::string errorMsg = "Метод getResponse вернул некорректный результат. Status: " +
                    std::to_string(static_cast<int>(response.status)) +
                        ", Count: " + std::to_string(response.count);
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при тестировании getResponse: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testGetJsonResponse() {
        std::cout << "Тестирование метода getJsonResponse..." << std::endl;
        std::string currentTestName = "testGetJsonResponse";
        totalTestsRun++;

        try {
            builder->clearRequestData();
            builder->setMethod("GET")
                    .setTable("users")
                    .addFilter("status='active'")
                    .addOrderBy("name ASC")
                    .setLimit(10);

            std::string json_response = builder->getJsonResponse();

            if (json_response.find("\"status\":\"200\"") != std::string::npos &&
                json_response.find("\"count\":") != std::string::npos) {
                std::cout << "  ✓ Метод getJsonResponse работает корректно" << std::endl;
            } else {
                std::string errorMsg = "Метод getJsonResponse вернул некорректный JSON: " + json_response;
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при тестировании getJsonResponse: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }

    void testParseUrlQuery() {
        std::cout << "Тестирование метода RequestData::parseUrlQuery..." << std::endl;
        std::string currentTestName = "testParseUrlQuery";
        totalTestsRun++;

        try {
            {
                RequestData requestData;
                std::string url_query =
                        "query=(method:GET;table:users;fields:[id,name,email];filters:[age>18,status='active'])";

                requestData.parseUrlQuery(url_query);

                if (requestData.method == HttpMethod::GET &&
                    requestData.table == "users" &&
                    requestData.fields.size() == 3 &&
                    requestData.filters.size() == 2) {
                    std::cout << "  ✓ Формат query={...} парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "Формат query={...} не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }

            {
                RequestData requestData;
                std::string url_query =
                        "{method:GET;table:users;fields:{id,name,email};filters:{age>18,status='active'}}";

                requestData.parseUrlQuery(url_query);

                if (requestData.method == HttpMethod::GET &&
                    requestData.table == "users" &&
                    requestData.fields.size() == 3 &&
                    requestData.filters.size() == 2) {
                    std::cout << "  ✓ Формат {...} парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "Формат {...} не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }

            {
                RequestData requestData;
                std::string url_query =
                        "query=(method:GET;table:orders;joins:[JOIN users ON orders.user_id=users.id,JOIN products ON orders.product_id=products.id])";

                requestData.parseUrlQuery(url_query);

                if (requestData.method == HttpMethod::GET &&
                    requestData.table == "orders" &&
                    requestData.joins.size() == 2) {
                    std::cout << "  ✓ Запрос с JOIN парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "Запрос с JOIN не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }

            {
                RequestData requestData;
                std::string url_query =
                        "query=(method:GET;table:sales;groups:[product_id];orders:[total_sales DESC];limit:10)";

                requestData.parseUrlQuery(url_query);

                if (requestData.method == HttpMethod::GET &&
                    requestData.table == "sales" &&
                    requestData.groups.size() == 1 &&
                    requestData.orders.size() == 1 &&
                    requestData.limit_value == 10) {
                    std::cout << "  ✓ Запрос с группировкой и сортировкой парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "Запрос с группировкой и сортировкой не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }

            {
                RequestData requestData;
                std::string url_query =
                        "query=(method:POST;table:users;data:[name=John Doe,email=john@example.com,age=25])";

                requestData.parseUrlQuery(url_query);

                if (requestData.method == HttpMethod::POST &&
                    requestData.table == "users" &&
                    requestData.data.contains("name") &&
                    requestData.data.contains("email") &&
                    requestData.data.contains("age")) {
                    std::cout << "  ✓ INSERT запрос парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "INSERT запрос не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }

            {
                RequestData requestData;
                std::string url_query =
                        "query=(method:PATCH;table:users;data:[email=newemail@example.com];filters:[id=123])";

                requestData.parseUrlQuery(url_query);

                if (requestData.method == HttpMethod::PATCH &&
                    requestData.table == "users" &&
                    requestData.data.contains("email") &&
                    requestData.filters.size() == 1) {
                    std::cout << "  ✓ UPDATE запрос парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "UPDATE запрос не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }

            {
                RequestData requestData;
                std::string url_query = "query=(method:DELETE;table:users;filters:[status='inactive'])";

                requestData.parseUrlQuery(url_query);

                if (requestData.method == HttpMethod::DELETE &&
                    requestData.table == "users" &&
                    requestData.filters.size() == 1) {
                    std::cout << "  ✓ DELETE запрос парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "DELETE запрос не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }

            {
                RequestData requestData;
                std::string full_url =
                        "http://localhost/api/data?query=(method:GET;table:users;filters:[age>18])&other_param=value";

                requestData.parseUrlQuery(full_url);

                if (requestData.method == HttpMethod::GET &&
                    requestData.table == "users" &&
                    requestData.filters.size() == 1) {
                    std::cout << "  ✓ Полный URL с query параметром парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "Полный URL с query параметром не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }

            {
                RequestData requestData;
                std::string url_query =
                        "query=(method:GET;table:employees;fields:[id,name,department,salary];filters:[department='IT',salary>50000];groups:[department];orders:[salary DESC];limit:20)";

                requestData.parseUrlQuery(url_query);

                if (requestData.method == HttpMethod::GET &&
                    requestData.table == "employees" &&
                    requestData.fields.size() == 4 &&
                    requestData.filters.size() == 2 &&
                    requestData.groups.size() == 1 &&
                    requestData.orders.size() == 1 &&
                    requestData.limit_value == 20) {
                    std::cout << "  ✓ Сложный запрос парсится корректно" << std::endl;
                } else {
                    std::string errorMsg = "Сложный запрос не парсится корректно";
                    std::cout << "  ✗ " << errorMsg << std::endl;
                    reportError(currentTestName, errorMsg);
                }
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при тестировании parseUrlQuery: " << e.what() << std::endl;
            reportError(currentTestName, e.what());
        }
    }
};

int main() {
    QueryBuilderTest test;
    return test.runAllTests() ? 0 : 1;
}
