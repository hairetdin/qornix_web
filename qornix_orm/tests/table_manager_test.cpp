/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <algorithm>
#include <iostream>
#include <set>

#include "handler_interface.h"
#include "schema_loader.h"
#include "database_interface.h"
#include "test_utils.h"

struct TestError {
    std::string testName;
    std::string errorMessage;
    std::string location;
};

class TableManagerTest {
private:
    std::unique_ptr<QornixHandler> handler_;
    std::shared_ptr<DatabaseInterface> db;
    std::unique_ptr<qornix_orm_tests::TempSqliteConfig> sqliteConfig;

    int totalErrors = 0;
    int totalTestsRun = 0;
    std::vector<TestError> errorDetails;  // Detali errors

    void reportError(const std::string& testName, const std::string& message) {
        TestError error;
        error.testName = testName;
        error.errorMessage = message;
        error.location = "Line: " + std::to_string(__LINE__);
        errorDetails.push_back(error);
        totalErrors++;
    }

    void createTestTables() {
        // Create test tables
        db->executeNonQuery("DROP TABLE IF EXISTS test_products");
        db->executeNonQuery("DROP TABLE IF EXISTS test_categories");

        db->executeNonQuery(R"(
            CREATE TABLE test_categories (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL
            )
        )");

        db->executeNonQuery(R"(
            CREATE TABLE test_products (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                test_categories_id INTEGER REFERENCES test_categories(id),
                price DECIMAL
            )
        )");
    }

    void insertTestData() {
        // Vstavka test data
        std::map<std::string, std::string> category_data = {
            {"name", "Тестовая категория"},
        };
        db->table("test_categories").create(category_data);


        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Тестовый продукт"},
            {"test_categories_id", "1"},
            {"price", "99.99"}
        });
    }

    void clearTestData() {
        // Remove data from related tables in correct order (first children)
        db->executeNonQuery("DELETE FROM test_products");
        db->executeNonQuery("DELETE FROM test_categories");
    }

    void cleanupTestTables() {
        db->executeNonQuery("DROP TABLE IF EXISTS test_products");
        db->executeNonQuery("DROP TABLE IF EXISTS test_categories");
    }

public:
    void setUp() {
        handler_ = std::make_unique<QornixHandler>();
        sqliteConfig = std::make_unique<qornix_orm_tests::TempSqliteConfig>("table_manager_test");

        if (!handler_->init(sqliteConfig->configPath())) {
            std::cerr << "Failed to initialize ORM handler with any config file" << std::endl;
            throw std::runtime_error("ORM initialization failed");
        }

        if (!handler_->db->isConnected()) {
            std::cerr << "Failed to connect to database" << std::endl;
            throw std::runtime_error("Database connection failed");
        }
        db = handler_->db;
        createTestTables();
        insertTestData();
        handler_->syncEntityWithDatabase(db);
    }

    void tearDown() {
        cleanupTestTables();
        if (db && db->isConnected()) {
            db->disconnect();
        }
        sqliteConfig.reset();
    }

    bool runAllTests() {
        setUp();

        testSchemaMetadataMethods();
        testBasicCRUD();
        testFilters();
        testFilterByRelatedEntities();
        testJoins();
        testGroupByAndAggregates();
        testUpdatesAndDeletes();
        testComplexQueries();
        testReverseRelationships(); // Test reverse relations
        testReverseRelationshipQueries(); // Test requests with obratnymi svyazyami

        tearDown();
        // Output results
        if (totalErrors == 0) {
            std::cout << "✓ Все " << totalTestsRun << " тестов пройдены успешно!" << std::endl;
        } else {
            std::cout << "\n❌ Ошибки: " << totalErrors << " из " << totalTestsRun << " тестов завершились с ошибками!\n" << std::endl;

            std::cout << "=== Детали ошибок ===" << std::endl;
            for (size_t i = 0; i < errorDetails.size(); ++i) {
                std::cout << (i + 1) << ". [" << errorDetails[i].testName << "] "
                          << errorDetails[i].errorMessage << std::endl;
            }

            std::cout << "\n=== Сводка по тестам с ошибками ===" << std::endl;
            std::set<std::string> failedTestNames;
            for (const auto& error : errorDetails) {
                failedTestNames.insert(error.testName);
            }

            for (const auto& testName : failedTestNames) {
                int errorCount = std::count_if(errorDetails.begin(), errorDetails.end(),
                    [&testName](const TestError& e) { return e.testName == testName; });
                std::cout << "- " << testName << ": " << errorCount << " ошибок" << std::endl;
            }
        }

        return totalErrors == 0;
    }

    void testSchemaMetadataMethods() {
        std::cout << "Тестирование методов getTableNames/getTableFields..." << std::endl;
        std::string currentTestName = "testSchemaMetadataMethods";
        totalTestsRun++;

        try {
            auto tableNames = db->getTableNames();
            bool hasCategories = std::find(tableNames.begin(), tableNames.end(), "test_categories") != tableNames.end();
            bool hasProducts = std::find(tableNames.begin(), tableNames.end(), "test_products") != tableNames.end();

            if (!hasCategories || !hasProducts) {
                std::string errorMsg = "Список таблиц не содержит test_categories/test_products";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
                return;
            }

            auto fields = db->getTableFields("test_products");
            bool hasId = std::find(fields.begin(), fields.end(), "id") != fields.end();
            bool hasName = std::find(fields.begin(), fields.end(), "name") != fields.end();
            bool hasPrice = std::find(fields.begin(), fields.end(), "price") != fields.end();

            if (!hasId || !hasName || !hasPrice) {
                std::string errorMsg = "Список полей test_products не содержит id/name/price";
                std::cout << "  ✗ " << errorMsg << std::endl;
                reportError(currentTestName, errorMsg);
                return;
            }

            std::cout << "  ✓ Методы метаданных таблиц работают корректно" << std::endl;
        } catch (const std::exception& e) {
            std::string errorMsg = "Ошибка при проверке метаданных: " + std::string(e.what());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }
    }

    void testBasicCRUD() {
        std::cout << "Тестирование CRUD операций..." << std::endl;
        std::string currentTestName = "testBasicCRUD";
        totalTestsRun++;

        // Test creation record
        std::cout << "  Тест создания записи..." << std::endl;
        std::map<std::string, std::string> category_data = {
            {"name", "Тестовая категория CRUD"}
        };
        auto created_category = db->table("test_categories").create(category_data);
        std::cout << "  Создана категория с ID: " << created_category["id"] << std::endl;

        // Test read record
        std::cout << "  Тест чтения записи..." << std::endl;
        auto retrieved_category = db->table("test_categories").get("id=" + created_category["id"]);
        if (retrieved_category["name"] == "Тестовая категория CRUD") {
            std::cout << "  ✓ Чтение работает корректно" << std::endl;
        } else {
            std::cout << "  ✗ Ошибка при чтении" << std::endl;
        }

        // Test update record
        std::cout << "  Тест обновления записи..." << std::endl;
        int updated_rows = db->table("test_categories")
                .filter("id=" + created_category["id"])
                .update({{"name", "Обновленная категория CRUD"}});

        if (updated_rows > 0) {
            std::cout << "  ✓ Обновление выполнено успешно" << std::endl;

            // Check update
            auto updated_category = db->table("test_categories").get("id=" + created_category["id"]);
            if (updated_category["name"] == "Обновленная категория CRUD") {
                std::cout << "  ✓ Данные после обновления корректны" << std::endl;
            } else {
                std::cout << "  ✗ Данные после обновления некорректны" << std::endl;
            }
        } else {
            std::cout << "  ✗ Обновление не выполнено" << std::endl;
        }

        // Test deletion record
        std::cout << "  Тест удаления записи..." << std::endl;
        int deleted_rows = db->table("test_categories")
                .filter("id=" + created_category["id"])
                .remove();

        if (deleted_rows > 0) {
            std::cout << "  ✓ Удаление выполнено успешно" << std::endl;
        } else {
            std::cout << "  ✗ Удаление не выполнено" << std::endl;
        }

        std::cout << "  CRUD тест завершен" << std::endl;
    }

    void testFilters() {
        std::cout << "Тестирование фильтров..." << std::endl;
        std::string currentTestName = "testFilters";
        totalTestsRun++;

        clearTestData(); // Cleanup before test

        // Preparation test data
        auto category1 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Фильтр Тест 1"}});
        auto category2 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Фильтр Тест 2"}});
        auto category3 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Другая категория"}});

        // Test simple filter by znacheniyu
        std::cout << "  Тест простого фильтра по значению..." << std::endl;
        auto results1 = db->table("test_categories").filter("name", "Фильтр Тест 1").all();
        if (results1.size() == 1 && results1[0]["name"] == "Фильтр Тест 1") {
            std::cout << "  ✓ Простой фильтр работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Простой фильтр не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results1.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        // Test filter with operator "equals"
        std::cout << "  Тест фильтра с оператором равно..." << std::endl;
        auto results2 = db->table("test_categories").filter("name=Фильтр Тест 2").all();
        if (results2.size() == 1 && results2[0]["name"] == "Фильтр Тест 2") {
            std::cout << "  ✓ Фильтр с оператором равно работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Фильтр с оператором равно не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results2.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        // Test filter with operator "greater" (__gt)
        std::cout << "  Тест фильтра с оператором больше..." << std::endl;
        // First create products with different prices
        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Продукт с высокой ценой"},
            {"test_categories_id", category1["id"]},
            {"price", "150.00"}
        });
        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Продукт с низкой ценой"},
            {"test_categories_id", category1["id"]},
            {"price", "50.00"}
        });

        auto results3 = db->table("test_products").filter("price__gt=100").all();
        if (results3.size() == 1 && results3[0]["name"] == "Продукт с высокой ценой") {
            std::cout << "  ✓ Фильтр с оператором больше работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Фильтр с оператором больше не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results3.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        // Test filter with operator "less" (__lt)
        std::cout << "  Тест фильтра с оператором меньше..." << std::endl;
        auto results4 = db->table("test_products").filter("price__lt=100").all();
        if (results4.size() == 1 && results4[0]["name"] == "Продукт с низкой ценой") {
            std::cout << "  ✓ Фильтр с оператором меньше работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Фильтр с оператором меньше не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results4.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        // Test filter with operator "not equals" (__ne)
        std::cout << "  Тест фильтра с оператором не равно..." << std::endl;
        auto results5 = db->table("test_categories").filter("name__ne=Другая категория").all();
        if (results5.size() == 2) {
            std::cout << "  ✓ Фильтр с оператором 'не равно' работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Фильтр с оператором 'не равно' не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results5.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        // Test filter with operator "occurrence" (__in)
        std::cout << "  Тест фильтра с оператором вхождение..." << std::endl;
        auto results6 = db->table("test_categories").filter("name__in='Фильтр Тест 1','Фильтр Тест 2'").all();
        if (results6.size() == 2) {
            std::cout << "  ✓ Фильтр с оператором вхождение работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Фильтр с оператором вхождение не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results6.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        // Test filter with LIKE
        std::cout << "  Тест фильтра с LIKE..." << std::endl;
        auto results7 = db->table("test_categories").filter("name__like=%Фильтр%").all();
        if (results7.size() == 2) {
            std::cout << "  ✓ Фильтр с LIKE работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Фильтр с LIKE не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results7.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        // Test slozhnogo filter with AND
        std::cout << "  Тест сложного фильтра с AND..." << std::endl;
        auto results8 = db->table("test_products").filter("name=Продукт с высокой ценой & price__gt=100").all();
        if (results8.size() == 1) {
            std::cout << "  ✓ Сложный фильтр с AND работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Сложный фильтр с AND не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results8.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        // Test filter with Map
        std::cout << "  Тест фильтра с Map..." << std::endl;
        auto results9 = db->table("test_categories").filter(std::map<std::string, std::string>{
            {"name", "Фильтр Тест 1"}
        }).all();
        if (results9.size() == 1) {
            std::cout << "  ✓ Фильтр с Map работает корректно" << std::endl;
        } else {
            std::string errorMsg = "Фильтр с Map не работает. Ожидаемый результат: 1 запись, получено: " +
                              std::to_string(results9.size());
            std::cout << "  ✗ " << errorMsg << std::endl;
            reportError(currentTestName, errorMsg);
        }

        std::cout << "  Тест фильтров завершен" << std::endl;
    }

    void testFilterByRelatedEntities() {
        std::cout << "Тестирование фильтрации по связанным сущностям..." << std::endl;
        std::string currentTestName = "testFilterByRelatedEntities";
        totalTestsRun++;

        // Preparation test data
        clearTestData(); // Cleanup before test

        // Create several categories
        auto category1 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Электроника"}});
        auto category2 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Одежда"}});
        auto category3 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Книги"}});

        // Create products in different categories
        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Ноутбук"},
            {"test_categories_id", category1["id"]},
            {"price", "50000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Телефон"},
            {"test_categories_id", category1["id"]},
            {"price", "30000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Футболка"},
            {"test_categories_id", category2["id"]},
            {"price", "2000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Джинсы"},
            {"test_categories_id", category2["id"]},
            {"price", "5000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Книга по C++"},
            {"test_categories_id", category3["id"]},
            {"price", "1500"}
        });

        // Test 1: filtering products by name category
        std::cout << "  Тест 1: Фильтрация продуктов по имени категории..." << std::endl;
        auto filtered_results1 = db->table("test_products")
                .filter("test_categories.name=Электроника")
                .values({"test_products.name", "test_products.price", "test_categories.name as category_name"})
                .execute();

        if (filtered_results1.size() == 2) {
            std::cout << "  ✓ Фильтрация продуктов по имени категории работает корректно" << std::endl;
            for (const auto &record: filtered_results1) {
                std::cout << "    Продукт: " << record.at("test_products.name")
                        << ", Цена: " << record.at("test_products.price")
                        << ", Категория: " << record.at("category_name") << std::endl;
            }
        } else {
            std::cout << "  ✗ Фильтрация продуктов по имени категории не работает корректно. Получено: "
                    << filtered_results1.size() << " записей" << std::endl;
        }

        // Test 2: filtering products by range prices in opredelennoy category
        std::cout << "  Тест 2: Фильтрация продуктов по диапазону цен в определенной категории..." << std::endl;
        auto filtered_results2 = db->table("test_products")
                .filter("test_categories.name=Одежда & test_products.price__gte=3000")
                .values({"test_products.name", "test_products.price", "test_categories.name as category_name"})
                .execute();

        if (filtered_results2.size() == 1 && filtered_results2[0]["test_products.name"] == "Джинсы") {
            std::cout << "  ✓ Фильтрация продуктов по диапазону цен в определенной категории работает корректно" <<
                    std::endl;
        } else {
            std::cout << "  ✗ Фильтрация продуктов по диапазону цен в определенной категории не работает корректно" <<
                    std::endl;
        }

        // Test 3: filtering categories by svoystvam related products
        std::cout << "  Тест 3: Фильтрация категорий по свойствам связанных продуктов..." << std::endl;
        auto filtered_results3 = db->table("test_categories")
                .filter("test_products.price__gt=20000")
                .values({"test_categories.name as category_name", "COUNT(test_products.id) as product_count"})
                .group_by("test_categories.name")
                .execute();

        if (filtered_results3.size() == 1 && filtered_results3[0]["category_name"] == "Электроника") {
            std::cout << "  ✓ Фильтрация категорий по свойствам связанных продуктов работает корректно" << std::endl;
        } else {
            std::cout << "  ✗ Фильтрация категорий по свойствам связанных продуктов не работает корректно" << std::endl;
        }

        // Test 4: filtering using operator IN for related table
        std::cout << "  Тест 4: Фильтрация с использованием оператора IN для связанной таблицы..." << std::endl;
        auto filtered_results4 = db->table("test_products")
                .filter("test_categories.name__in='Электроника','Книги'")
                .values({"test_products.name", "test_products.price", "test_categories.name as category_name"})
                .order_by("test_categories.name, test_products.price")
                .execute();

        if (filtered_results4.size() == 3) {
            // 2 product in "Electronics" + 1 in "Books"
            std::cout << "  ✓ Фильтрация с использованием IN для связанной таблицы работает корректно" << std::endl;
            for (const auto &record: filtered_results4) {
                std::cout << "    Продукт: " << record.at("test_products.name")
                        << ", Цена: " << record.at("test_products.price")
                        << ", Категория: " << record.at("category_name") << std::endl;
            }
        } else {
            std::cout << "  ✗ Фильтрация с использованием IN для связанной таблицы не работает корректно. Получено: "
                    << filtered_results4.size() << " записей" << std::endl;
        }

        // Test 5: filtering using LIKE for related table
        std::cout << "  Тест 5: Фильтрация с использованием LIKE для связанной таблицы..." << std::endl;
        auto filtered_results5 = db->table("test_products")
                .filter("test_categories.name__like=%роник%") // Contains "ronic" - for "Electronics"
                .values({"test_products.name", "test_products.price", "test_categories.name as category_name"})
                .execute();

        if (filtered_results5.size() == 2) {
            std::cout << "  ✓ Фильтрация с использованием LIKE для связанной таблицы работает корректно" << std::endl;
        } else {
            std::cout << "  ✗ Фильтрация с использованием LIKE для связанной таблицы не работает корректно. Получено: "
                    << filtered_results5.size() << " записей" << std::endl;
        }

        std::cout << "  Тест фильтрации по связанным сущностям завершен" << std::endl;
    }


    void testJoins() {
        std::cout << "Тестирование JOIN операций..." << std::endl;
        std::string currentTestName = "testJoins";
        totalTestsRun++;

        clearTestData();

        // Preparation test data
        auto category1 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Электроника"}});
        auto category2 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Одежда"}});

        auto product1 = db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Ноутбук"},
            {"test_categories_id", category1["id"]},
            {"price", "50000"}
        });

        auto product2 = db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Футболка"},
            {"test_categories_id", category2["id"]},
            {"price", "2000"}
        });

        // Test 1: INNER JOIN using method join()
        std::cout << "  Тест INNER JOIN с явным вызовом join()..." << std::endl;
        auto join_results1 = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category_name", "test_products.name as product_name", "test_products.price"
                })
                .execute();

        if (join_results1.size() == 2) {
            std::cout << "  ✓ INNER JOIN работает корректно" << std::endl;
            for (const auto &record: join_results1) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Продукт: " << record.at("product_name")
                        << ", Цена: " << record.at("test_products.price")
                        << std::endl;
            }
        } else {
            std::cout << "  ✗ INNER JOIN вернул неправильное количество записей" << std::endl;
        }

        // Test 2: JOIN with filtering
        std::cout << "  Тест JOIN с фильтрацией..." << std::endl;
        auto join_results2 = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category_name", "test_products.name as product_name", "test_products.price"
                })
                .filter("test_products.price__gt=10000")
                .execute();

        if (join_results2.size() == 1 && join_results2[0]["product_name"] == "Ноутбук") {
            std::cout << "  ✓ JOIN с фильтрацией работает корректно" << std::endl;
        } else {
            std::cout << "  ✗ JOIN с фильтрацией не работает корректно" << std::endl;
        }

        // Test 3: Avtomaticheskiy JOIN through sintaksis fields with dot
        std::cout << "  Тест автоматического JOIN через поля с точкой..." << std::endl;
        auto join_results3 = db->table("test_categories")
                .values({"name", "test_products.name", "test_products.price"})
                .execute();

        if (join_results3.size() == 2) {
            std::cout << "  ✓ Автоматический JOIN работает корректно" << std::endl;
            for (const auto &record: join_results3) {
                std::cout << "    Категория: " << record.at("name")
                        << ", Продукт: " << record.at("test_products.name")
                        << ", Цена: " << record.at("test_products.price") << std::endl;
            }
        } else {
            std::cout << "  ✗ Автоматический JOIN не работает корректно" << std::endl;
        }

        // Test 4: JOIN with filtering by related table
        std::cout << "  Тест JOIN с фильтрацией по связанной таблице..." << std::endl;
        auto join_results4 = db->table("test_categories")
                .values({"name", "test_products.name", "test_products.price"})
                .filter("test_products.price__lt=5000")
                .execute();

        if (join_results4.size() == 1 && join_results4[0]["test_products.name"] == "Футболка") {
            std::cout << "  ✓ JOIN с фильтрацией по связанной таблице работает корректно" << std::endl;
        } else {
            std::cout << "  ✗ JOIN с фильтрацией по связанной таблице не работает корректно" << std::endl;
        }

        // Test 5: JOIN with aggregate functions
        std::cout << "  Тест JOIN с агрегатными функциями..." << std::endl;
        auto join_results5 = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category_name", "COUNT(test_products.id) as product_count",
                    "AVG(test_products.price) as avg_price"
                })
                .group_by("test_categories.name")
                .execute();

        if (join_results5.size() == 2) {
            std::cout << "  ✓ JOIN с агрегатными функциями работает корректно" << std::endl;
            for (const auto &record: join_results5) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Количество продуктов: " << record.at("product_count")
                        << ", Средняя цена: " << record.at("avg_price") << std::endl;
            }
        } else {
            std::cout << "  ✗ JOIN с агрегатными функциями не работает корректно" << std::endl;
        }

        std::cout << "  Тест JOIN операций завершен" << std::endl;
    }


    void testGroupByAndAggregates() {
        std::cout << "Тестирование GROUP BY и агрегатных функций..." << std::endl;
        std::string currentTestName = "testGroupByAndAggregates";
        totalTestsRun++;

        clearTestData();

        // Preparation test data
        auto category1 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Электроника"}});
        auto category2 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Одежда"}});

        // Create products with different prices for testing agregatnykh functions
        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Ноутбук"},
            {"test_categories_id", category1["id"]},
            {"price", "50000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Телефон"},
            {"test_categories_id", category1["id"]},
            {"price", "30000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Футболка"},
            {"test_categories_id", category2["id"]},
            {"price", "2000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Джинсы"},
            {"test_categories_id", category2["id"]},
            {"price", "5000"}
        });

        // Test 1: COUNT - podschet number of products in each category
        std::cout << "  Тест COUNT агрегатной функции..." << std::endl;
        auto count_results = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({"test_categories.name as category_name", "COUNT(test_products.id) as product_count"})
                .group_by("test_categories.name")
                .execute();

        if (count_results.size() == 2) {
            std::cout << "  ✓ COUNT агрегатная функция работает корректно" << std::endl;
            for (const auto &record: count_results) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Количество продуктов: " << record.at("product_count") << std::endl;
            }
        } else {
            std::cout << "  ✗ COUNT агрегатная функция не работает корректно" << std::endl;
        }

        // Test 2: SUM - summa prices products in each category
        std::cout << "  Тест SUM агрегатной функции..." << std::endl;
        auto sum_results = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({"test_categories.name as category_name", "SUM(test_products.price) as total_price"})
                .group_by("test_categories.name")
                .execute();

        if (sum_results.size() == 2) {
            std::cout << "  ✓ SUM агрегатная функция работает корректно" << std::endl;
            for (const auto &record: sum_results) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Общая стоимость: " << record.at("total_price") << std::endl;
            }
        } else {
            std::cout << "  ✗ SUM агрегатная функция не работает корректно" << std::endl;
        }

        // Test 3: AVG - srednyaya price products in each category
        std::cout << "  Тест AVG агрегатной функции..." << std::endl;
        auto avg_results = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({"test_categories.name as category_name", "AVG(test_products.price) as avg_price"})
                .group_by("test_categories.name")
                .execute();

        if (avg_results.size() == 2) {
            std::cout << "  ✓ AVG агрегатная функция работает корректно" << std::endl;
            for (const auto &record: avg_results) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Средняя цена: " << record.at("avg_price") << std::endl;
            }
        } else {
            std::cout << "  ✗ AVG агрегатная функция не работает корректно" << std::endl;
        }

        // Test 4: MIN and MAX - minimalnaya and maksimalnaya price products in each category
        std::cout << "  Тест MIN и MAX агрегатных функций..." << std::endl;
        auto minmax_results = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category_name",
                    "MIN(test_products.price) as min_price",
                    "MAX(test_products.price) as max_price"
                })
                .group_by("test_categories.name")
                .execute();

        if (minmax_results.size() == 2) {
            std::cout << "  ✓ MIN и MAX агрегатные функции работают корректно" << std::endl;
            for (const auto &record: minmax_results) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Минимальная цена: " << record.at("min_price")
                        << ", Максимальная цена: " << record.at("max_price") << std::endl;
            }
        } else {
            std::cout << "  ✗ MIN и MAX агрегатные функции не работают корректно" << std::endl;
        }

        // Test 5: GROUP BY with filtering through HAVING
        std::cout << "  Тест GROUP BY с HAVING фильтрацией..." << std::endl;
        auto having_results = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({"test_categories.name as category_name", "COUNT(test_products.id) as product_count"})
                .group_by("test_categories.name")
                .having("COUNT(test_products.id) > 1")
                .execute();

        if (having_results.size() == 2) {
            std::cout << "  ✓ GROUP BY с HAVING фильтрацией работает корректно" << std::endl;
            for (const auto &record: having_results) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Количество продуктов: " << record.at("product_count") << std::endl;
            }
        } else {
            std::cout << "  ✗ GROUP BY с HAVING фильтрацией не работает корректно" << std::endl;
        }

        // Test 6: GROUP BY with ORDER BY
        std::cout << "  Тест GROUP BY с ORDER BY..." << std::endl;
        auto order_results = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({"test_categories.name as category_name", "AVG(test_products.price) as avg_price"})
                .group_by("test_categories.name")
                .order_by("avg_price DESC")
                .execute();

        if (order_results.size() == 2) {
            std::cout << "  ✓ GROUP BY с ORDER BY работает корректно" << std::endl;
            for (const auto &record: order_results) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Средняя цена: " << record.at("avg_price") << std::endl;
            }
        } else {
            std::cout << "  ✗ GROUP BY с ORDER BY не работает корректно" << std::endl;
        }

        std::cout << "  Тест GROUP BY и агрегатных функций завершен" << std::endl;
    }


    void testUpdatesAndDeletes() {
        std::cout << "Тестирование UPDATE и DELETE операций..." << std::endl;
        std::string currentTestName = "testUpdatesAndDeletes";
        totalTestsRun++;

        clearTestData(); // Cleanup before test

        // Preparation test data
        auto category1 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Обновляемая категория"}});
        auto category2 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Удаляемая категория"}});

        auto product1 = db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Обновляемый продукт"},
            {"test_categories_id", category1["id"]},
            {"price", "1000"}
        });

        auto product2 = db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Удаляемый продукт"},
            {"test_categories_id", category2["id"]},
            {"price", "2000"}
        });

        // Test 1: Update odnoy record through Method update()
        std::cout << "  Тест 1 обновления одной записи..." << std::endl;
        int updated_rows = db->table("test_categories")
                .filter("id=" + category1["id"])
                .update({{"name", "Обновленная категория"}});

        if (updated_rows == 1) {
            std::cout << "  ✓ Обновление одной записи выполнено успешно" << std::endl;

            // Check update
            auto updated_category = db->table("test_categories").get("id=" + category1["id"]);
            if (updated_category["name"] == "Обновленная категория") {
                std::cout << "  ✓ Данные после обновления корректны" << std::endl;
            } else {
                std::cout << "  ✗ Данные после обновления некорректны" << std::endl;
            }
        } else {
            std::cout << "  ✗ Обновление одной записи не выполнено" << std::endl;
        }

        // Test 2: Update multiple records through filter
        std::cout << "  Тест 2 обновления нескольких записей через фильтр..." << std::endl;
        auto new_product1 = db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Дешевый продукт 1"},
            {"test_categories_id", category1["id"]},
            {"price", "500"}
        });

        auto new_product2 = db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Дешевый продукт 2"},
            {"test_categories_id", category1["id"]},
            {"price", "600"}
        });

        int updated_multiple = db->table("test_products")
                .filter("price__lt=1000")
                .update({{"price", "800"}});

        if (updated_multiple == 2) {
            std::cout << "  ✓ Обновление нескольких записей выполнено успешно" << std::endl;

            // Check update
            auto cheap_products = db->table("test_products").filter("price=800").all();
            if (cheap_products.size() == 2) {
                std::cout << "  ✓ Данные после массового обновения корректны" << std::endl;
            } else {
                std::cout << "  ✗ Данные после массового обновения некорректны" << std::endl;
            }
        } else {
            std::cout << "  ✗ Обновление нескольких записей не выполнено корректно" << std::endl;
        }

        // Test 3: Update through string data
        std::cout << "  Тест 3 обновления через строку данных..." << std::endl;
        int updated_string = db->table("test_products")
                .filter("id=" + product1["id"])
                .update("name='Обновленный продукт', price=1500");

        if (updated_string == 1) {
            std::cout << "  ✓ Обновление через строку данных выполнено успешно" << std::endl;

            // Check update
            auto updated_product = db->table("test_products").get("id=" + product1["id"]);
            if (updated_product["name"] == "Обновленный продукт" && updated_product["price"] == "1500.00") {
                std::cout << "  ✓ Данные после строкового обновления корректны" << std::endl;
            } else {
                std::cout << "  ✗ Данные после строкового обновения некорректны" << std::endl;
            }
        } else {
            std::cout << "  ✗ Обновление через строку данных не выполнено" << std::endl;
        }

        // Test 4: Remove odnoy record through Method remove()
        std::cout << "  Тест 4 удаления одной записи..." << std::endl;
        int deleted_rows = db->table("test_products")
                .filter("id=" + product2["id"])
                .remove();

        if (deleted_rows == 1) {
            std::cout << "  ✓ Удаление одной записи выполнено успешно" << std::endl;

            // Check deletion
            try {
                auto deleted_product = db->table("test_products").get("id=" + product2["id"]);
                std::cout << "  ✗ Запись не была удалена" << std::endl;
            } catch (const std::exception &) {
                std::cout << "  ✓ Запись действительно удалена" << std::endl;
            }
        } else {
            std::cout << "  ✗ Удаление одной записи не выполнено" << std::endl;
        }

        // Test 5: Remove multiple records through filter
        std::cout << "  Тест 5 удаления нескольких записей через фильтр..." << std::endl;
        auto delete_product1 = db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Временный продукт 1"},
            {"test_categories_id", category1["id"]},
            {"price", "3000"}
        });

        auto delete_product2 = db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Временный продукт 2"},
            {"test_categories_id", category1["id"]},
            {"price", "4000"}
        });

        int deleted_multiple = db->table("test_products")
                .filter("price__gt=2500")
                .remove();

        if (deleted_multiple == 2) {
            std::cout << "  ✓ Удаление нескольких записей выполнено успешно" << std::endl;

            // Check deletion
            auto remaining_expensive = db->table("test_products").filter("price__gt=2500").all();
            if (remaining_expensive.size() == 0) {
                std::cout << "  ✓ Записи действительно удалены" << std::endl;
            } else {
                std::cout << "  ✗ Записи не были удалены корректно" << std::endl;
            }
        } else {
            std::cout << "  ✗ Удаление нескольких записей не выполнено корректно" << std::endl;
        }

        // Test 6: check, that other record ostalis netronutymi
        std::cout << "  Тест 6 проверки целостности оставшихся данных..." << std::endl;
        auto remaining_products = db->table("test_products").all();
        auto remaining_categories = db->table("test_categories").all();

        if (remaining_products.size() > 0 && remaining_categories.size() > 0) {
            std::cout << "  ✓ Остальные данные остались нетронутыми" << std::endl;
        } else {
            std::cout << "  ✗ Остальные данные были затронуты" << std::endl;
        }

        std::cout << "  Тест UPDATE и DELETE операций завершен" << std::endl;
    }


    void testComplexQueries() {
        std::cout << "Тестирование сложных запросов..." << std::endl;
        std::string currentTestName = "testComplexQueries";
        totalTestsRun++;

        clearTestData(); // Cleanup before test

        // Preparation test data
        auto category1 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Электроника"}});
        auto category2 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Одежда"}});
        auto category3 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Книги"}});

        // Create products with different kharakteristikami
        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Ноутбук"},
            {"test_categories_id", category1["id"]},
            {"price", "50000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Телефон"},
            {"test_categories_id", category1["id"]},
            {"price", "30000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Планшет"},
            {"test_categories_id", category1["id"]},
            {"price", "25000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Футболка"},
            {"test_categories_id", category2["id"]},
            {"price", "2000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Джинсы"},
            {"test_categories_id", category2["id"]},
            {"price", "5000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Книга по C++"},
            {"test_categories_id", category3["id"]},
            {"price", "1500"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Книга по Python"},
            {"test_categories_id", category3["id"]},
            {"price", "1200"}
        });

        // Test 1: complex request with JOIN, WHERE, GROUP BY and HAVING
        std::cout << "  Тест 1: Сложный запрос с JOIN, WHERE, GROUP BY и HAVING..." << std::endl;
        auto complex_results1 = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category_name",
                    "COUNT(test_products.id) as product_count",
                    "AVG(test_products.price) as avg_price"
                })
                .filter("test_products.price__gte=2000")
                .group_by("test_categories.name")
                .having("COUNT(test_products.id) >= 1")
                .order_by("avg_price DESC")
                .execute();

        if (complex_results1.size() > 0) {
            std::cout << "  ✓ Сложный запрос с JOIN, WHERE, GROUP BY и HAVING работает корректно" << std::endl;
            for (const auto &record: complex_results1) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Количество продуктов: " << record.at("product_count")
                        << ", Средняя цена: " << record.at("avg_price") << std::endl;
            }
        } else {
            std::cout << "  ✗ Сложный запрос с JOIN, WHERE, GROUP BY и HAVING не работает корректно" << std::endl;
        }

        // Test 2: complex filter with logicheskimi operators
        std::cout << "  Тест 2: Сложный фильтр с логическими операторами..." << std::endl;
        auto complex_results2 = db->table("test_products")
                .join("test_categories", "test_products.test_categories_id = test_categories.id")
                .filter(
                    "(test_categories.name=Электроника | test_categories.name=Книги) & test_products.price__gt=1000"
                )
                .values({
                    "test_categories.name as category", "test_products.name as product", "test_products.price as price"
                })
                .order_by("test_categories.name, test_products.price DESC")
                .execute();

        if (complex_results2.size() == 5) {
            // 3 product in "Electronics" + 2 in "Books"
            std::cout << "  ✓ Сложный фильтр с логическими операторами работает корректно" << std::endl;
            for (const auto &record: complex_results2) {
                std::cout << "    Категория: " << record.at("category")
                        << ", Продукт: " << record.at("product")
                        << ", Цена: " << record.at("price") << std::endl;
            }
        } else {
            std::cout << "  ✗ Сложный фильтр с логическими операторами не работает корректно. Получено: "
                    << complex_results2.size() << " записей" << std::endl;
        }

        // Test 3: nested request (through podzapros in filtre) - emulation through JOIN
        std::cout << "  Тест 3: Запрос с подсчетом и фильтрацией по агрегатной функции..." << std::endl;
        auto complex_results3 = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category_name",
                    "COUNT(test_products.id) as product_count"
                })
                .group_by("test_categories.name")
                .having(
                    "COUNT(test_products.id) = (SELECT MAX(cnt) FROM (SELECT COUNT(id) as cnt FROM test_products GROUP BY test_categories_id) as counts)")
                .execute();

        if (complex_results3.size() > 0) {
            std::cout << "  ✓ Запрос с фильтрацией по агрегатной функции работает корректно" << std::endl;
            for (const auto &record: complex_results3) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Количество продуктов: " << record.at("product_count") << std::endl;
            }
        } else {
            std::cout << "  ✗ Запрос с фильтрацией по агрегатной функции не работает корректно" << std::endl;
        }

        // Test 4: request with LIMIT and OFFSET
        std::cout << "  Тест 4: Запрос с LIMIT и ORDER BY..." << std::endl;
        auto complex_results4 = db->table("test_products")
                .join("test_categories", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category", "test_products.name as product", "test_products.price as price"
                })
                .order_by("test_products.price DESC")
                .limit(3)
                .execute();

        if (complex_results4.size() == 3) {
            std::cout << "  ✓ Запрос с LIMIT работает корректно" << std::endl;
            for (const auto &record: complex_results4) {
                std::cout << "    Категория: " << record.at("category")
                        << ", Продукт: " << record.at("product")
                        << ", Цена: " << record.at("price") << std::endl;
            }
        } else {
            std::cout << "  ✗ Запрос с LIMIT не работает корректно" << std::endl;
        }

        // Test 5: complex request with neskolkimi usloviyami filtering
        std::cout << "  Тест 5: Сложный запрос с несколькими условиями фильтрации..." << std::endl;
        auto complex_results5 = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category_name",
                    "test_products.name as product_name",
                    "test_products.price as price"
                })
                .filter("test_categories.name__like=%о%") // Category with bukvoy "o"
                // .filter("test_products.price__between=1000,35000") // price from 1000 before 35000
                .order_by("test_categories.name, test_products.price DESC")
                .execute();

        if (complex_results5.size() > 0) {
            std::cout << "  ✓ Сложный запрос с несколькими условиями фильтрации работает корректно" << std::endl;
            for (const auto &record: complex_results5) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Продукт: " << record.at("product_name")
                        << ", Цена: " << record.at("price") << std::endl;
            }
        } else {
            std::cout << "  ✗ Сложный запрос с несколькими условиями фильтрации не работает корректно" << std::endl;
        }

        // Test 6: request with agregatsiey and filtering before and after grouping
        std::cout << "  Тест 6: Запрос с агрегацией и фильтрацией до и после группировки..." << std::endl;
        auto complex_results6 = db->table("test_categories")
                .join("test_products", "test_products.test_categories_id = test_categories.id")
                .values({
                    "test_categories.name as category_name",
                    "COUNT(test_products.id) as total_products",
                    "AVG(test_products.price) as avg_price",
                    "MIN(test_products.price) as min_price",
                    "MAX(test_products.price) as max_price"
                })
                .filter("test_products.price__gt=1000") // Filtering before grouping
                .group_by("test_categories.name")
                .having("AVG(test_products.price) > 5000") // Filtering after grouping
                .order_by("avg_price DESC")
                .execute();

        if (complex_results6.size() > 0) {
            std::cout << "  ✓ Запрос с агрегацией и фильтрацией до и после группировки работает корректно" << std::endl;
            for (const auto &record: complex_results6) {
                std::cout << "    Категория: " << record.at("category_name")
                        << ", Всего продуктов: " << record.at("total_products")
                        << ", Средняя цена: " << record.at("avg_price")
                        << ", Мин. цена: " << record.at("min_price")
                        << ", Макс. цена: " << record.at("max_price") << std::endl;
            }
        } else {
            std::cout << "  ✗ Запрос с агрегацией и фильтрацией до и после группировки не работает корректно" <<
                    std::endl;
        }

        std::cout << "  Тест сложных запросов завершен" << std::endl;
    }

    void testReverseRelationships() {
        std::cout << "Тестирование обратных связей после syncEntityWithDatabase..." << std::endl;
        std::string currentTestName = "testReverseRelationships";
        totalTestsRun++;

        // Update schema from database
        handler_->syncEntityWithDatabase(db);

        // Get updated entity
        const auto &entities = SchemaLoader::getEntities();

        // Check presence entity categories
        auto categoryIt = entities.find("test_categories");
        if (categoryIt != entities.end()) {
            const auto &categoryEntity = categoryIt->second;

            std::cout << "  Найдена сущность: " << categoryEntity.name << std::endl;
            std::cout << "  Количество обратных связей: " << categoryEntity.reverseRelations.size() << std::endl;

            // Check presence reverse relation from products
            bool foundReverseRelation = false;
            for (const auto &reverseRel: categoryEntity.reverseRelations) {
                std::cout << "    Обратная связь: " << reverseRel.name
                        << " (тип: " << reverseRel.type
                        << ", ссылается на: " << reverseRel.references
                        << ", isReverseRelation: " << reverseRel.isReverseRelation << ")" << std::endl;

                // Check that this really reverse relation
                if (reverseRel.isReverseRelation &&
                    reverseRel.type == "RELATED_KEYS" &&
                    reverseRel.references == "test_products") {
                    foundReverseRelation = true;
                    std::cout << "  ✓ Найдена корректная обратная связь: " << reverseRel.name << std::endl;
                    break;
                }
            }

            if (!foundReverseRelation) {
                std::cout << "  ✗ Обратная связь не найдена или некорректна" << std::endl;
            }
        } else {
            std::cout << "  ✗ Сущность test_categories не найдена" << std::endl;
        }

        // Check that entity products contains foreign key
        auto productIt = entities.find("test_products");
        if (productIt != entities.end()) {
            const auto &productEntity = productIt->second;

            std::cout << "  Внешние ключи в test_products: " << productEntity.foreignKeys.size() << std::endl;
            for (const auto &fk: productEntity.foreignKeys) {
                std::cout << "    FK: " << fk.name
                        << " -> " << fk.references
                        << "." << fk.toField << std::endl;
            }
        }

        std::cout << "  Тест обратных связей завершен" << std::endl;
    }

    void testReverseRelationshipQueries() {
        std::cout << "Тестирование запросов с обратными связями..." << std::endl;
        std::string currentTestName = "testReverseRelationshipQueries";
        totalTestsRun++;


        // Update schema from database
        handler_->syncEntityWithDatabase(db);

        // Preparation test data
        auto category1 = db->table("test_categories").create(
            std::map<std::string, std::string>{{"name", "Категория с продуктами"}});

        // Create several products for this category
        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Продукт 1"},
            {"test_categories_id", category1["id"]},
            {"price", "1000"}
        });

        db->table("test_products").create(std::map<std::string, std::string>{
            {"name", "Продукт 2"},
            {"test_categories_id", category1["id"]},
            {"price", "2000"}
        });

        // Check request using reverse relation
        try {
            // Request for getting category and all related products
            auto results = db->table("test_categories")
                    .filter("id=" + category1["id"])
                    .values({"name", "test_products.name", "test_products.price"})
                    .execute();

            if (!results.empty()) {
                std::cout << "  ✓ Запрос с обратной связью выполнен успешно" << std::endl;
                std::cout << "  Найдено записей: " << results.size() << std::endl;

                for (const auto &record: results) {
                    for (const auto &field: record) {
                        std::cout << "    " << field.first << ": " << field.second << std::endl;
                    }
                }
            } else {
                std::cout << "  ✗ Запрос с обратной связью не вернул данных" << std::endl;
            }
        } catch (const std::exception &e) {
            std::cout << "  ✗ Ошибка при выполнении запроса с обратной связью: " << e.what() << std::endl;
        }

        std::cout << "  Тест запросов с обратными связями завершен" << std::endl;
    }
};

int main() {
    TableManagerTest test;
    return test.runAllTests() ? 0 : 1;
}
