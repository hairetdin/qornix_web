/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace dynamic_entity_server_example {

inline void executeDemoSql(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = error ? error : "unknown SQLite error";
        if (error) {
            sqlite3_free(error);
        }
        throw std::runtime_error(message);
    }
}

// Ensure demo database is created only once, thread-safely
inline void ensureDemoDatabase() {
    static std::once_flag init_flag;
    static bool in_progress = false;

    std::call_once(init_flag, []() {
        std::string dbPath;

        // Try to find the SQLite database relative to the executable
        char exePath[4096];
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len != -1) {
            exePath[len] = '\0';
            std::filesystem::path exeDir(exePath);
            exeDir = exeDir.parent_path();

            std::vector<std::string> candidates = {
                (exeDir / "dynamic_entity_demo.sqlite3").string(),
                (exeDir / "example" / "dynamic_entity_server" / "dynamic_entity_demo.sqlite3").string(),
                "dynamic_entity_demo.sqlite3",
            };

            for (const auto& candidate : candidates) {
                if (std::filesystem::exists(candidate)) {
                    dbPath = candidate;
                    break;
                }
            }
        }

        if (dbPath.empty()) {
            dbPath = "dynamic_entity_demo.sqlite3";
        }

        std::filesystem::path dbPathFs(dbPath);
        const bool existed = std::filesystem::exists(dbPathFs);

        if (!dbPathFs.parent_path().empty()) {
            std::filesystem::create_directories(dbPathFs.parent_path());
        }

        sqlite3* db = nullptr;
        const int rc = sqlite3_open(dbPath.c_str(), &db);
        if (rc != SQLITE_OK) {
            std::string message = db ? sqlite3_errmsg(db) : "unknown SQLite open error";
            if (db) {
                sqlite3_close(db);
            }
            throw std::runtime_error("Failed to open demo SQLite database: " + message);
        }

        try {
            executeDemoSql(db, R"SQL(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA busy_timeout = 5000;

CREATE TABLE IF NOT EXISTS categories (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS customers (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE,
    phone TEXT,
    city TEXT,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS products (
    id INTEGER PRIMARY KEY,
    category_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    description TEXT,
    price REAL NOT NULL,
    stock INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    FOREIGN KEY (category_id) REFERENCES categories(id)
);

CREATE TABLE IF NOT EXISTS orders (
    id INTEGER PRIMARY KEY,
    customer_id INTEGER NOT NULL,
    product_id INTEGER NOT NULL,
    order_date TEXT NOT NULL,
    quantity INTEGER NOT NULL,
    status TEXT NOT NULL,
    total_amount REAL NOT NULL,
    FOREIGN KEY (customer_id) REFERENCES customers(id),
    FOREIGN KEY (product_id) REFERENCES products(id)
);

INSERT OR IGNORE INTO categories (id, name, description, created_at) VALUES
    (1, 'Electronics', 'Devices and accessories', '2026-01-10'),
    (2, 'Books', 'Printed and digital books', '2026-01-11'),
    (3, 'Home', 'Home and kitchen goods', '2026-01-12'),
    (4, 'Grocery', 'Everyday grocery items', '2026-01-13');

INSERT OR IGNORE INTO customers (id, name, email, phone, city, created_at) VALUES
    (1, 'Alice Johnson', 'alice@example.com', '+1-555-0101', 'Austin', '2026-02-01'),
    (2, 'Bob Smith', 'bob@example.com', '+1-555-0102', 'Seattle', '2026-02-03'),
    (3, 'Carol Davis', 'carol@example.com', '+1-555-0103', 'Denver', '2026-02-05'),
    (4, 'David Brown', 'david@example.com', '+1-555-0104', 'Boston', '2026-02-07');

INSERT OR IGNORE INTO products (id, category_id, name, description, price, stock, created_at) VALUES
    (1, 1, 'Laptop Pro 14', 'Portable laptop for developers', 1299.99, 12, '2026-02-10'),
    (2, 1, 'Wireless Mouse', 'Bluetooth ergonomic mouse', 39.50, 80, '2026-02-11'),
    (3, 2, 'SQL Handbook', 'Practical SQL examples', 29.90, 35, '2026-02-12'),
    (4, 3, 'Coffee Maker', 'Compact drip coffee maker', 89.00, 18, '2026-02-13'),
    (5, 4, 'Green Tea', 'Loose leaf green tea', 12.40, 120, '2026-02-14');

INSERT OR IGNORE INTO orders (id, customer_id, product_id, order_date, quantity, status, total_amount) VALUES
    (1, 1, 1, '2026-03-01', 1, 'paid', 1299.99),
    (2, 1, 2, '2026-03-01', 2, 'paid', 79.00),
    (3, 2, 3, '2026-03-03', 1, 'shipped', 29.90),
    (4, 3, 4, '2026-03-05', 1, 'pending', 89.00),
    (5, 4, 5, '2026-03-08', 4, 'paid', 49.60),
    (6, 2, 1, '2026-03-12', 1, 'cancelled', 1299.99);
)SQL");
        } catch (...) {
            sqlite3_close(db);
            throw;
        }

        sqlite3_close(db);
        std::cout << "SQLite demo database " << (existed ? "ready" : "created")
                  << ": " << dbPath << std::endl;
    });
}

inline DatabaseConfig demoDatabaseConfig() {
    ensureDemoDatabase();

    DatabaseConfig config{};
    config.driver = "sqlite";
    config.dbname = "dynamic_entity_demo.sqlite3";
    config.connectionString = "dynamic_entity_demo.sqlite3";
    return config;
}

} // namespace dynamic_entity_server_example
