/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

// entity_api_controller_test.cpp
#include <iostream>
#include <cassert>
#include <memory>
#include <boost/json.hpp>

#include "handler_interface.h"
#include "entity_api_controller.h"

#include "schema_loader.h"
#include "test_utils.h"

class EntityAPIControllerTest {
private:
    std::unique_ptr<QornixHandler> handler_;
    std::unique_ptr<EntityAPIController> api_controller_;
    std::unique_ptr<qornix_orm_tests::TempSqliteConfig> sqliteConfig_;

public:
    void setUp() {
        handler_ = std::make_unique<QornixHandler>();
        sqliteConfig_ = std::make_unique<qornix_orm_tests::TempSqliteConfig>("entity_api_controller_test");

        if (!handler_->init(sqliteConfig_->configPath())) {
            std::cerr << "Failed to initialize ORM handler with any config file" << std::endl;
            throw std::runtime_error("ORM initialization failed");
        }

        if (!handler_->db->isConnected()) {
            std::cerr << "Failed to connect to database" << std::endl;
            throw std::runtime_error("Database connection failed");
        }

        // Cleanup test tables before sozdaniem novykh
        cleanupTestTables();

        // Create related entities and tables in database data
        createLinkedEntities();
        handler_->syncEntityWithDatabase(handler_->db);

        api_controller_ = std::make_unique<EntityAPIController>(handler_->db);
    }

    void cleanupTestTables() {
        auto db = handler_->db;

        // Remove table in correct order (first dependent)
        try {
            db->executeNonQuery("DROP TABLE IF EXISTS orders");
            db->executeNonQuery("DROP TABLE IF EXISTS products");
            db->executeNonQuery("DROP TABLE IF EXISTS categories");
        } catch (const std::exception &e) {
            std::cerr << "Error during table cleanup: " << e.what() << std::endl;
        }
    }


    // Method for creation related entities and sootvetstvuyuschikh tables
    void createLinkedEntities() {
        auto db = handler_->db;

        // Create entity Category
        EntityDefinition categoryEntity;
        categoryEntity.name = "category";
        categoryEntity.tableName = "categories";
        categoryEntity.verboseName = "Category";
        categoryEntity.verboseNamePlural = "Categories";

        // Add fields for Category
        FieldDefinition categoryIdField;
        categoryIdField.name = "id";
        categoryIdField.type = "INTEGER";
        categoryIdField.primaryKey = true;
        categoryIdField.autoIncrement = true;
        categoryEntity.fields.push_back(categoryIdField);

        FieldDefinition categoryNameField;
        categoryNameField.name = "name";
        categoryNameField.type = "VARCHAR";
        categoryNameField.maxLength = "255";
        categoryEntity.fields.push_back(categoryNameField);

        FieldDefinition categoryDescField;
        categoryDescField.name = "description";
        categoryDescField.type = "TEXT";
        categoryDescField.nullable = true;
        categoryEntity.fields.push_back(categoryDescField);

        // Create table Category in database data
        if (!SchemaLoader::createDBTable(db, categoryEntity)) {
            std::cerr << "Failed to create table for category" << std::endl;
            throw std::runtime_error("Table creation failed for category");
        }

        // Create entity Product
        EntityDefinition productEntity;
        productEntity.name = "product";
        productEntity.tableName = "products";
        productEntity.verboseName = "Product";
        productEntity.verboseNamePlural = "Products";

        // Add fields for Product
        FieldDefinition productIdField;
        productIdField.name = "id";
        productIdField.type = "INTEGER";
        productIdField.primaryKey = true;
        productIdField.autoIncrement = true;
        productEntity.fields.push_back(productIdField);

        FieldDefinition productNameField;
        productNameField.name = "name";
        productNameField.type = "VARCHAR";
        productNameField.maxLength = "255";
        productEntity.fields.push_back(productNameField);

        FieldDefinition productPriceField;
        productPriceField.name = "price";
        productPriceField.type = "DECIMAL";
        productPriceField.precision = "10";
        productPriceField.scale = "2";
        productEntity.fields.push_back(productPriceField);

        // Column for foreign key must be opredelen as regular field
        FieldDefinition categoryFkField;
        categoryFkField.name = "category_id";
        categoryFkField.type = "INTEGER";
        categoryFkField.nullable = false; // or true in depending from trebovaniy
        productEntity.fields.push_back(categoryFkField); // Add as regular field

        // Then Determine foreign key
        ForeignKeyField categoryForeignKey;
        categoryForeignKey.name = "category_id";
        categoryForeignKey.type = "INTEGER";
        categoryForeignKey.references = "categories";
        categoryForeignKey.toField = "id";
        categoryForeignKey.onDelete = "CASCADE";
        categoryForeignKey.onUpdate = "CASCADE";
        productEntity.foreignKeys.push_back(categoryForeignKey); // Add as foreign key

        // Create table Product in database data
        if (!SchemaLoader::createDBTable(db, productEntity)) {
            std::cerr << "Failed to create table for product" << std::endl;
            throw std::runtime_error("Table creation failed for product");
        }

        // Create entity Order
        EntityDefinition orderEntity;
        orderEntity.name = "order";
        orderEntity.tableName = "orders";
        orderEntity.verboseName = "Order";
        orderEntity.verboseNamePlural = "Orders";

        // Add fields for Order
        FieldDefinition orderIdField;
        orderIdField.name = "id";
        orderIdField.type = "INTEGER";
        orderIdField.primaryKey = true;
        orderIdField.autoIncrement = true;
        orderEntity.fields.push_back(orderIdField);

        FieldDefinition orderDateField;
        orderDateField.name = "order_date";
        orderDateField.type = "TIMESTAMP";
        orderDateField.defaultValue = "CURRENT_TIMESTAMP";
        orderEntity.fields.push_back(orderDateField);

        FieldDefinition orderQuantityField;
        orderQuantityField.name = "quantity";
        orderQuantityField.type = "INTEGER";
        orderEntity.fields.push_back(orderQuantityField);

        // Foreign key on Product - first Determine column, then foreign key
        FieldDefinition productFkField;
        productFkField.name = "product_id";
        productFkField.type = "INTEGER";
        productFkField.nullable = false;
        orderEntity.fields.push_back(productFkField); // Add as regular field

        // Then Determine foreign key
        ForeignKeyField productForeignKey;
        productForeignKey.name = "product_id";
        productForeignKey.type = "INTEGER";
        productForeignKey.references = "products";
        productForeignKey.toField = "id";
        productForeignKey.onDelete = "CASCADE";
        productForeignKey.onUpdate = "CASCADE";
        orderEntity.foreignKeys.push_back(productForeignKey); // Add as foreign key

        // Create table Order in database data
        if (!SchemaLoader::createDBTable(db, orderEntity)) {
            std::cerr << "Failed to create table for order" << std::endl;
            throw std::runtime_error("Table creation failed for order");
        }

        std::cout << "Linked entities and tables created successfully" << std::endl;
    }


    void tearDown() {
        cleanupTestTables();
        api_controller_.reset();
        handler_.reset();
        sqliteConfig_.reset();
    }

    void testBasicCRUD() {
        std::cout << "Running testBasicCRUD..." << std::endl;

        // Test creation category (POST)
        std::string categoryData = R"({"name": "Electronics", "description": "Electronic products"})";
        std::string createCategoryResult = api_controller_->handlePostRequest("categories", categoryData);
        assert(createCategoryResult.find("error") == std::string::npos);
        std::cout << "Create category operation passed" << std::endl;

        // Get ID created category
        boost::json::value parsedCat = boost::json::parse(createCategoryResult);
        boost::json::array catResultArray = parsedCat.as_array();
        std::string categoryId = catResultArray[0].as_object().at("id").as_string().c_str();

        // Test creation product (POST) with vneshnim klyuchom
        std::string productData = R"({"name": "Laptop", "price": "999.99", "category_id": ")" + categoryId + R"("})";
        std::string createProductResult = api_controller_->handlePostRequest("products", productData);
        assert(createProductResult.find("error") == std::string::npos);
        std::cout << "Create product operation passed" << std::endl;

        // Get ID created product
        boost::json::value parsedProd = boost::json::parse(createProductResult);
        boost::json::array prodResultArray = parsedProd.as_array();
        std::string productId = prodResultArray[0].as_object().at("id").as_string().c_str();

        // Test getting product (GET)
        std::map<std::string, std::string> params = {{"id", productId}};
        std::string getProductResult = api_controller_->handleGetRequest("products", params);
        assert(getProductResult.find("Laptop") != std::string::npos);
        std::cout << "Read product operation passed" << std::endl;

        // Test update product (PUT)
        std::string updateData = R"({"name": "Updated Laptop", "price": "899.99", "category_id": ")" + categoryId +
                                 R"("})";
        std::string updateResult = api_controller_->handlePutRequest("products", productId, updateData);
        assert(updateResult.find("error") == std::string::npos);

        // Check update
        std::string updatedResult = api_controller_->handleGetRequest("products", params);
        assert(updatedResult.find("Updated Laptop") != std::string::npos);
        std::cout << "Update operation passed" << std::endl;

        // Test partial update product (PATCH) - change only tseny
        std::string patchData = R"({"price": "799.99"})"; // Update only tsenu, not trogaya other fields
        std::string patchResult = api_controller_->handlePatchRequest("products", productId, patchData);
        assert(patchResult.find("error") == std::string::npos);

        // Check partial update
        std::string patchedResult = api_controller_->handleGetRequest("products", params);
        assert(patchedResult.find("Updated Laptop") != std::string::npos); // Name must ostatsya prezhnim
        assert(patchedResult.find("799.99") != std::string::npos); // Price must izmenitsya
        std::cout << "Partial update (PATCH) operation passed" << std::endl;

        // Test deletion product (DELETE)
        std::string deleteResult = api_controller_->handleDeleteRequest("products", productId);
        assert(deleteResult.find("success") != std::string::npos);

        // Check deletion
        std::string afterDeleteResult = api_controller_->handleGetRequest("products", params);
        assert(afterDeleteResult.find("[]") != std::string::npos);
        std::cout << "Delete operation passed" << std::endl;
    }

    void testListAll() {
        std::cout << "Running testListAll..." << std::endl;

        // Create multiple categories
        for (int i = 0; i < 3; ++i) {
            std::string categoryData = R"({"name": "Category)" + std::to_string(i) +
                                       R"(", "description": "Test category )" + std::to_string(i) + R"("})";
            api_controller_->handlePostRequest("categories", categoryData);
        }

        // Test getting all categories (GET without parameters)
        std::map<std::string, std::string> emptyParams;
        std::string listResult = api_controller_->handleGetRequest("categories", emptyParams);

        // Check that is returned array with 3+ elementami
        assert(listResult.find("Category0") != std::string::npos);
        assert(listResult.find("Category1") != std::string::npos);
        assert(listResult.find("Category2") != std::string::npos);
        std::cout << "List all operation passed" << std::endl;
    }

    void testFilters() {
        std::cout << "Running testFilters..." << std::endl;

        // Create products for testing filters
        std::vector<std::string> products = {
            R"({"name": "Laptop", "price": "1000", "category_id": "1"})",
            R"({"name": "Phone", "price": "500", "category_id": "1"})",
            R"({"name": "Tablet", "price": "300", "category_id": "2"})"
        };

        for (const auto &productData: products) {
            api_controller_->handlePostRequest("products", productData);
        }

        // Test filtering by name
        std::map<std::string, std::string> filters = {{"name", "Laptop"}};
        std::string filterResult = api_controller_->handleGetRequest("products", filters);

        // Check that result contains only product with imenem Laptop
        assert(filterResult.find("Laptop") != std::string::npos);
        assert(filterResult.find("Phone") == std::string::npos); // Not must be in rezultatakh
        std::cout << "Filter operation passed" << std::endl;
    }

    void testLinkedEntityFilters() {
        std::cout << "Running testLinkedEntityFilters..." << std::endl;

        // Create category
        std::string categoryData = R"({"name": "Electronics", "description": "Electronic items"})";
        std::string createCategoryResult = api_controller_->handlePostRequest("categories", categoryData);
        boost::json::value parsedCat = boost::json::parse(createCategoryResult);
        boost::json::array catResultArray = parsedCat.as_array();
        std::string categoryId = catResultArray[0].as_object().at("id").as_string().c_str();

        // Create products in this category
        std::string product1Data = R"({"name": "High-end Laptop", "price": "2000", "category_id": ")" + categoryId +
                                   R"("})";
        std::string product2Data = R"({"name": "Budget Laptop", "price": "500", "category_id": ")" + categoryId +
                                   R"("})";
        api_controller_->handlePostRequest("products", product1Data);
        api_controller_->handlePostRequest("products", product2Data);

        // Test filtering products by svyazannoy category (through foreign key)
        std::map<std::string, std::string> filters = {{"category_id", categoryId}};
        std::string linkedFilterResult = api_controller_->handleGetRequest("products", filters);

        // Check that result contains both product from category
        assert(linkedFilterResult.find("High-end Laptop") != std::string::npos);
        assert(linkedFilterResult.find("Budget Laptop") != std::string::npos);
        std::cout << "Linked entity filter operation passed" << std::endl;
    }

    void testAdvancedFilters() {
        std::cout << "Running testAdvancedFilters..." << std::endl;

        // Create products with different prices
        std::vector<std::string> products = {
            R"({"name": "Expensive Product", "price": "1000", "category_id": "1"})",
            R"({"name": "Medium Product", "price": "500", "category_id": "1"})",
            R"({"name": "Cheap Product", "price": "100", "category_id": "1"})"
        };

        for (const auto &productData: products) {
            api_controller_->handlePostRequest("products", productData);
        }

        // Test filtering by tsene (> 400)
        std::map<std::string, std::string> greaterThanFilters = {{"price__gt", "400"}};
        std::string greaterThanResult = api_controller_->handleGetRequest("products", greaterThanFilters);

        // Check that result contains only expensive and medium-priced products
        assert(greaterThanResult.find("Expensive Product") != std::string::npos);
        assert(greaterThanResult.find("Medium Product") != std::string::npos);
        assert(greaterThanResult.find("Cheap Product") == std::string::npos);
        std::cout << "Advanced filter (greater than) operation passed" << std::endl;

        // Test filtering by range prices (mezhdu 200 and 800)
        std::map<std::string, std::string> betweenFilters = {{"price__gte", "200"}, {"price__lte", "800"}};
        std::string betweenResult = api_controller_->handleGetRequest("products", betweenFilters);

        // Check that result contains only medium-priced product
        assert(betweenResult.find("Medium Product") != std::string::npos);
        assert(betweenResult.find("Expensive Product") == std::string::npos);
        assert(betweenResult.find("Cheap Product") == std::string::npos);
        std::cout << "Advanced filter (between) operation passed" << std::endl;
    }

    void testLimitAndOffset() {
        std::cout << "Running testLimitAndOffset..." << std::endl;

        // Create multiple products
        for (int i = 0; i < 5; ++i) {
            std::string productData = R"({"name": "Product)" + std::to_string(i) + R"(", "price": ")" +
                                      std::to_string(100 + i * 100) + R"(", "category_id": "1"})";
            api_controller_->handlePostRequest("products", productData);
        }

        // Test limit number of results with sorting by ID by descending
        std::map<std::string, std::string> limitParams = {{"limit", "2"}, {"order_by", "-id"}};
        std::string limitedResult = api_controller_->handleGetRequest("products", limitParams);

        // Check that result contains 2 product with highest ID
        size_t first = limitedResult.find("Product4");
        size_t second = limitedResult.find("Product3", first + 1);
        size_t third = limitedResult.find("Product2", second + 1);

        assert(first != std::string::npos);
        assert(second != std::string::npos);
        assert(third == std::string::npos); // Tretego not must be pri limite 2
        std::cout << "Limit operation with DESC ordering passed" << std::endl;
    }

    void testOrderWithProductAndCategoryDetails() {
        std::cout << "Running testOrderWithProductAndCategoryDetails..." << std::endl;

        // Create category
        std::string categoryData = R"({"name": "Electronics", "description": "Electronic products"})";
        std::string createCategoryResult = api_controller_->handlePostRequest("categories", categoryData);
        boost::json::value parsedCat = boost::json::parse(createCategoryResult);
        boost::json::array catResultArray = parsedCat.as_array();
        std::string categoryId = catResultArray[0].as_object().at("id").as_string().c_str();

        // Create product in this category
        std::string productData = R"({"name": "Smartphone", "price": "599.99", "category_id": ")" + categoryId +
                                  R"("})";
        std::string createProductResult = api_controller_->handlePostRequest("products", productData);
        boost::json::value parsedProd = boost::json::parse(createProductResult);
        boost::json::array prodResultArray = parsedProd.as_array();
        std::string productId = prodResultArray[0].as_object().at("id").as_string().c_str();

        // Create order for this product
        std::string orderData = R"({"quantity": "2", "product_id": ")" + productId + R"("})";
        std::string createOrderResult = api_controller_->handlePostRequest("orders", orderData);
        boost::json::value parsedOrder = boost::json::parse(createOrderResult);
        boost::json::array orderResultArray = parsedOrder.as_array();
        std::string orderId = orderResultArray[0].as_object().at("id").as_string().c_str();

        // Test getting order with details product and category, using parameter values
        std::map<std::string, std::string> params = {
            // {"id", orderId}, {"values", "id, product.name, product.price"}
            {"id", orderId}, {"values", "id, product.name, product.price, product.category.name"}
        };
        std::string orderResult = api_controller_->handleGetRequest("orders", params);

        // Check that order byl poluchen with nuzhnymi fields
        assert(orderResult.find("Smartphone") != std::string::npos);
        // assert(orderResult.find("Electronics") != std::string::npos);
        // Check that vozvraschayutsya only specified fields
        assert(
            orderResult.find("quantity") != std::string::npos || orderResult.find("order_date") == std::string::npos);
        // quantity is in zakaze, but not all fields
        std::cout << "Order with product and category details retrieved successfully using values parameter" <<
                std::endl;

        // Test getting all orders with rasshirennymi details, using parameter values
        std::map<std::string, std::string> allParams = {{"values", "id, product.name, product.category.name"}};
        // std::map<std::string, std::string> allParams = {{"values", "orders.id, products.name"}};
        std::string allOrdersResult = api_controller_->handleGetRequest("orders", allParams);

        // Check that result contains information o produkte and category
        assert(allOrdersResult.find("Smartphone") != std::string::npos);
        assert(allOrdersResult.find("Electronics") != std::string::npos);
        std::cout << "All orders with product and category details retrieved successfully using values parameter" <<
                std::endl;
    }


    void testOrdersWithJoinDetails() {
        std::cout << "Running testOrdersWithJoinDetails..." << std::endl;

        // Create multiple categories
        std::string cat1Data = R"({"name": "Electronics", "description": "Electronic products"})";
        std::string cat2Data = R"({"name": "Books", "description": "Book products"})";

        std::string cat1Result = api_controller_->handlePostRequest("categories", cat1Data);
        std::string cat2Result = api_controller_->handlePostRequest("categories", cat2Data);

        boost::json::value parsedCat1 = boost::json::parse(cat1Result);
        std::string cat1Id = parsedCat1.as_array()[0].as_object().at("id").as_string().c_str();

        boost::json::value parsedCat2 = boost::json::parse(cat2Result);
        std::string cat2Id = parsedCat2.as_array()[0].as_object().at("id").as_string().c_str();

        // Create products in different categories
        std::string prod1Data = R"({"name": "Laptop", "price": "999.99", "category_id": ")" + cat1Id + R"("})";
        std::string prod2Data = R"({"name": "Programming Book", "price": "29.99", "category_id": ")" + cat2Id + R"("})";

        std::string prod1Result = api_controller_->handlePostRequest("products", prod1Data);
        std::string prod2Result = api_controller_->handlePostRequest("products", prod2Data);

        boost::json::value parsedProd1 = boost::json::parse(prod1Result);
        std::string prod1Id = parsedProd1.as_array()[0].as_object().at("id").as_string().c_str();

        boost::json::value parsedProd2 = boost::json::parse(prod2Result);
        std::string prod2Id = parsedProd2.as_array()[0].as_object().at("id").as_string().c_str();

        // Create orders for etikh products
        std::string order1Data = R"({"quantity": "1", "product_id": ")" + prod1Id + R"("})";
        std::string order2Data = R"({"quantity": "3", "product_id": ")" + prod2Id + R"("})";

        api_controller_->handlePostRequest("orders", order1Data);
        api_controller_->handlePostRequest("orders", order2Data);

        // Test getting orders with sorting by ID order by descending
        std::map<std::string, std::string> params = {
            {"order_by", "-id"}, {"values", "id, product.name, product.price, product.category.name"}
        };
        std::string ordersResult = api_controller_->handleGetRequest("orders", params);

        // Check that result contains information o produktakh and categories
        assert(ordersResult.find("Laptop") != std::string::npos);
        assert(ordersResult.find("Programming Book") != std::string::npos);
        assert(ordersResult.find("Electronics") != std::string::npos);
        assert(ordersResult.find("Books") != std::string::npos);

        std::cout << "Orders with join details retrieved successfully" << std::endl;
    }


    bool runAllTests() {
        setUp();

        bool success = true;
        try {
            testBasicCRUD();
            testListAll();
            testFilters();
            testLinkedEntityFilters();
            testAdvancedFilters();
            testLimitAndOffset();
            testOrderWithProductAndCategoryDetails();
            testOrdersWithJoinDetails();

            std::cout << "Все тесты пройдены успешно!" << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "Тест не пройден: " << e.what() << std::endl;
            success = false;
        }

        tearDown();
        return success;
    }
};

int main() {
    EntityAPIControllerTest test;
    return test.runAllTests() ? 0 : 1;
}
