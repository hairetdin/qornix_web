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

std::string buildConnectionString(
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
    std::cout << "=== PostgreSQL Driver Integration Test ===" << std::endl;

    const std::string host = getEnvOrDefault("POSTGRES_TEST_HOST", "127.0.0.1");
    const std::string port = getEnvOrDefault("POSTGRES_TEST_PORT", "5432");
    const std::string user = getEnvOrEmpty("POSTGRES_TEST_USER");
    const std::string password = getEnvOrEmpty("POSTGRES_TEST_PASSWORD");
    const std::string configuredDb = getEnvOrEmpty("POSTGRES_TEST_DB");

    if (user.empty()) {
        std::cout << "SKIPPED: set POSTGRES_TEST_USER (and optionally POSTGRES_TEST_PASSWORD/HOST/PORT/DB) to run this test."
                  << std::endl;
        return 0;
    }

    const bool ownsDatabase = configuredDb.empty();
    const std::string dbName = ownsDatabase
        ? "qornix_postgres_driver_test_" + std::to_string(static_cast<long long>(std::time(nullptr)))
        : configuredDb;

    const std::string adminConnection = buildConnectionString(host, port, user, password, "postgres");
    const std::string dbConnection = buildConnectionString(host, port, user, password, dbName);

    try {
        if (ownsDatabase) {
            auto adminDb = DatabaseInterface::init(adminConnection, "postgresql");
            assertTrue(adminDb && adminDb->isConnected(), "admin connection established");
            adminDb->executeNonQuery("DROP DATABASE IF EXISTS \"" + dbName + "\"");
            adminDb->disconnect();
        }

        auto db = DatabaseInterface::init(dbConnection, "postgresql");
        assertTrue(db && db->isConnected(), "database connection established");

        db->executeNonQuery("DROP TABLE IF EXISTS users");
        db->executeNonQuery(
            "CREATE TABLE users ("
            "id SERIAL PRIMARY KEY,"
            "name VARCHAR(128) NOT NULL,"
            "email VARCHAR(128) NOT NULL UNIQUE,"
            "age INTEGER NOT NULL"
            ")"
        );
        std::cout << "  [INFO] created table users" << std::endl;

        DatabaseResponse createResp = db->exec(
            "INSERT INTO users (name, email, age) VALUES ($1, $2, $3)",
            {"Alice", "alice@example.com", "30"}
        );
        assertTrue(static_cast<int>(createResp.http_status) == 200, "create returns HTTP 200");
        assertTrue(createResp.affected_rows == 1, "create affected_rows == 1");

        DatabaseResponse readResp = db->exec(
            "SELECT id, name, email, age FROM users WHERE email = $1",
            {"alice@example.com"}
        );
        assertTrue(static_cast<int>(readResp.http_status) == 200, "read returns HTTP 200");
        assertTrue(readResp.count == 1, "read count == 1");
        assertTrue(readResp.data[0].at("name") == "Alice", "read returns expected name");

        DatabaseResponse updateResp = db->exec(
            "UPDATE users SET age = $1 WHERE email = $2",
            {"31", "alice@example.com"}
        );
        assertTrue(static_cast<int>(updateResp.http_status) == 200, "update returns HTTP 200");
        assertTrue(updateResp.affected_rows == 1, "update affected_rows == 1");

        DatabaseResponse deleteResp = db->exec(
            "DELETE FROM users WHERE email = $1",
            {"alice@example.com"}
        );
        assertTrue(static_cast<int>(deleteResp.http_status) == 200, "delete returns HTTP 200");
        assertTrue(deleteResp.affected_rows == 1, "delete affected_rows == 1");

        db->executeNonQuery("DROP TABLE IF EXISTS users");
        db->disconnect();

        if (ownsDatabase) {
            auto adminDb = DatabaseInterface::init(adminConnection, "postgresql");
            if (adminDb && adminDb->isConnected()) {
                adminDb->executeNonQuery("DROP DATABASE IF EXISTS \"" + dbName + "\"");
            }
        }

        std::cout << "=== PostgreSQL Driver Integration Test PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PostgreSQL driver test failed: " << e.what() << std::endl;
        if (ownsDatabase) {
            try {
                auto adminDb = DatabaseInterface::init(adminConnection, "postgresql");
                if (adminDb && adminDb->isConnected()) {
                    adminDb->executeNonQuery("DROP DATABASE IF EXISTS \"" + dbName + "\"");
                }
            } catch (...) {
            }
        }
        return 1;
    }
}
