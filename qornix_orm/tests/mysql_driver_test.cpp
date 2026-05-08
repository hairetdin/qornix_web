/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "database_interface.h"

namespace {
std::string getEnvOrDefault(const char* key, const std::string& fallback) {
    const char* value = std::getenv(key);
    if (!value) {
        return fallback;
    }
    return std::string(value);
}

std::string getEnvOrEmpty(const char* key) {
    const char* value = std::getenv(key);
    if (!value) {
        return "";
    }
    return std::string(value);
}

std::string buildAdminConnectionString(
    const std::string& host,
    const std::string& port,
    const std::string& user,
    const std::string& password
) {
    std::string cs = "host=" + host + " port=" + port + " user=" + user;
    if (!password.empty()) {
        cs += " password=" + password;
    }
    return cs;
}

std::string buildDbConnectionString(
    const std::string& host,
    const std::string& port,
    const std::string& user,
    const std::string& password,
    const std::string& dbName
) {
    std::string cs = "host=" + host + " port=" + port + " user=" + user + " dbname=" + dbName;
    if (!password.empty()) {
        cs += " password=" + password;
    }
    return cs;
}

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
    std::cout << "  [OK] " << message << std::endl;
}
} // namespace

int main() {
    std::cout << "=== MySQL Driver Integration Test ===" << std::endl;

    const std::string host = getEnvOrDefault("MYSQL_TEST_HOST", "127.0.0.1");
    const std::string port = getEnvOrDefault("MYSQL_TEST_PORT", "3306");
    const std::string user = getEnvOrEmpty("MYSQL_TEST_USER");
    const std::string password = getEnvOrEmpty("MYSQL_TEST_PASSWORD");

    if (user.empty()) {
        std::cout << "SKIPPED: set MYSQL_TEST_USER (and optionally MYSQL_TEST_PASSWORD/HOST/PORT) to run this test."
                  << std::endl;
        return 0;
    }

    const std::string dbName = "qornix_mysql_driver_test_" + std::to_string(static_cast<long long>(std::time(nullptr)));
    const std::string adminConnection = buildAdminConnectionString(host, port, user, password);
    const std::string dbConnection = buildDbConnectionString(host, port, user, password, dbName);

    try {
        // Admin connection for setup/teardown.
        auto adminDb = DatabaseInterface::init(adminConnection, "mysql");
        assertTrue(adminDb && adminDb->isConnected(), "admin connection established");

        // Ensure database does not exist before test (to validate auto-create logic).
        adminDb->executeNonQuery("DROP DATABASE IF EXISTS `" + dbName + "`");
        std::cout << "  [INFO] dropped pre-existing DB if any: " << dbName << std::endl;

        // Auto-create DB should happen in MySQLDriver::connect.
        auto db = DatabaseInterface::init(dbConnection, "mysql");
        assertTrue(db && db->isConnected(), "database connection established with auto-create");

        // CRUD schema setup.
        db->executeNonQuery(
            "CREATE TABLE users ("
            "id INT AUTO_INCREMENT PRIMARY KEY,"
            "name VARCHAR(128) NOT NULL,"
            "email VARCHAR(128) NOT NULL UNIQUE,"
            "age INT NOT NULL"
            ")"
        );
        std::cout << "  [INFO] created table users" << std::endl;

        // CREATE via exec(params)
        DatabaseResponse createResp = db->exec(
            "INSERT INTO users (name, email, age) VALUES ($1, $2, $3)",
            {"Alice", "alice@example.com", "30"}
        );
        assertTrue(static_cast<int>(createResp.http_status) == 200, "create returns HTTP 200");
        assertTrue(createResp.affected_rows == 1, "create affected_rows == 1");

        // READ via exec(params)
        DatabaseResponse readResp = db->exec(
            "SELECT id, name, email, age FROM users WHERE email = $1",
            {"alice@example.com"}
        );
        assertTrue(static_cast<int>(readResp.http_status) == 200, "read returns HTTP 200");
        assertTrue(readResp.count == 1, "read count == 1");
        assertTrue(readResp.data[0].at("name") == "Alice", "read returns expected name");

        // UPDATE via exec(params)
        DatabaseResponse updateResp = db->exec(
            "UPDATE users SET age = $1 WHERE email = $2",
            {"31", "alice@example.com"}
        );
        assertTrue(static_cast<int>(updateResp.http_status) == 200, "update returns HTTP 200");
        assertTrue(updateResp.affected_rows == 1, "update affected_rows == 1");

        DatabaseResponse verifyUpdateResp = db->exec(
            "SELECT age FROM users WHERE email = $1",
            {"alice@example.com"}
        );
        assertTrue(verifyUpdateResp.count == 1, "updated row exists");
        assertTrue(verifyUpdateResp.data[0].at("age") == "31", "updated age is persisted");

        // DELETE via exec(params)
        DatabaseResponse deleteResp = db->exec(
            "DELETE FROM users WHERE email = $1",
            {"alice@example.com"}
        );
        assertTrue(static_cast<int>(deleteResp.http_status) == 200, "delete returns HTTP 200");
        assertTrue(deleteResp.affected_rows == 1, "delete affected_rows == 1");

        DatabaseResponse verifyDeleteResp = db->exec(
            "SELECT id FROM users WHERE email = $1",
            {"alice@example.com"}
        );
        assertTrue(static_cast<int>(verifyDeleteResp.http_status) == 404, "read after delete returns HTTP 404");

        // Cleanup
        db->disconnect();
        adminDb->executeNonQuery("DROP DATABASE IF EXISTS `" + dbName + "`");
        std::cout << "  [INFO] dropped test DB: " << dbName << std::endl;

        std::cout << "=== MySQL Driver Integration Test PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "MySQL driver test failed: " << e.what() << std::endl;
        try {
            auto adminDb = DatabaseInterface::init(adminConnection, "mysql");
            if (adminDb && adminDb->isConnected()) {
                adminDb->executeNonQuery("DROP DATABASE IF EXISTS `" + dbName + "`");
            }
        } catch (...) {
            // Ignore cleanup failures in test failure path.
        }
        return 1;
    }
}
