/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "app_paths.h"
#include "runtime_config.h"
#include "handler_base.h"
#include "database_interface.h"
#include "config.h"

#include <fstream>
#include <vector>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <system_error>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

class DynamicAppPageHandler : public HandlerBase {
public:
    DynamicAppPageHandler(std::string templatePath, std::string fallbackHtml)
        : templatePath_(std::move(templatePath)), fallbackHtml_(std::move(fallbackHtml)) {}

protected:
    void handleGet(
        const http::request<http::string_body>&,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        buildHtmlResponse(res, http::status::ok, loadTemplate(templatePath_, fallbackHtml_));
    }

private:
    std::string templatePath_;
    std::string fallbackHtml_;

    static std::string loadTemplate(const std::string& path, const std::string& fallback) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return fallback;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};


namespace dynamic_api_app_demo {

inline DatabaseConfig databaseConfig() {
    return dynamic_api_app_runtime::config().databaseConfig;
}

inline std::string loadDemoSchemaXml() {
    const auto path = dynamic_api_app_paths::staticDir() / "demo_schema.xml";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline void writeDemoSchemaCopy(const std::string& xml) {
    const auto& path = dynamic_api_app_runtime::config().uploadedSchemaPath;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << xml;
}

inline void removePath(const std::filesystem::path& path, boost::json::object& removed, const char* key) {
    std::error_code ec;
    const bool existed = std::filesystem::exists(path, ec);
    if (existed) {
        std::filesystem::remove(path, ec);
    }
    boost::json::object item;
    item["path"] = path.string();
    item["existed"] = existed;
    item["removed"] = existed && !ec;
    item["error"] = ec ? ec.message() : "";
    removed[key] = item;
}

inline boost::json::object resetFiles() {
    const auto& runtime = dynamic_api_app_runtime::config();
    boost::json::object removed;
    removePath(runtime.databaseConfig.dbname, removed, "database");
    removePath(runtime.uploadedSchemaPath, removed, "uploaded_schema");
    removePath(runtime.exportedSchemaPath, removed, "exported_schema");
    removePath(runtime.historyPath, removed, "schema_history");
    return removed;
}

inline void execAll(DatabaseInterface& db, const std::vector<std::string>& sql) {
    for (const auto& statement : sql) {
        db.executeNonQuery(statement);
    }
}

inline boost::json::object createDemoDatabase() {
    auto dbUnique = DatabaseInterface::init(databaseConfig());
    std::shared_ptr<DatabaseInterface> db(std::move(dbUnique));

    execAll(*db, {
        "PRAGMA foreign_keys = ON",
        "CREATE TABLE categories (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE, description TEXT)",
        "CREATE TABLE customers (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, email TEXT NOT NULL UNIQUE, phone TEXT, city TEXT)",
        "CREATE TABLE products (id INTEGER PRIMARY KEY AUTOINCREMENT, category_id INTEGER, name TEXT NOT NULL, description TEXT, price REAL NOT NULL, stock INTEGER NOT NULL DEFAULT 0, FOREIGN KEY(category_id) REFERENCES categories(id) ON DELETE SET NULL)",
        "CREATE TABLE orders (id INTEGER PRIMARY KEY AUTOINCREMENT, customer_id INTEGER NOT NULL, product_id INTEGER NOT NULL, quantity INTEGER NOT NULL, status TEXT NOT NULL, total_amount REAL NOT NULL, FOREIGN KEY(customer_id) REFERENCES customers(id), FOREIGN KEY(product_id) REFERENCES products(id))",
        "INSERT INTO categories (id, name, description) VALUES (1, 'Hardware', 'Physical products')",
        "INSERT INTO categories (id, name, description) VALUES (2, 'Accessories', 'Add-ons and extras')",
        "INSERT INTO categories (id, name, description) VALUES (3, 'Office', 'Office equipment')",
        "INSERT INTO customers (id, name, email, phone, city) VALUES (1, 'Ada Lovelace', 'ada@example.test', '+1-555-0101', 'London')",
        "INSERT INTO customers (id, name, email, phone, city) VALUES (2, 'Grace Hopper', 'grace@example.test', '+1-555-0102', 'Arlington')",
        "INSERT INTO customers (id, name, email, phone, city) VALUES (3, 'Linus Torvalds', 'linus@example.test', '+1-555-0103', 'Helsinki')",
        "INSERT INTO products (id, category_id, name, description, price, stock) VALUES (1, 1, 'Wireless Mouse', 'Bluetooth ergonomic mouse', 39.90, 75)",
        "INSERT INTO products (id, category_id, name, description, price, stock) VALUES (2, 1, 'Mechanical Keyboard', 'Hot-swappable keyboard', 129.90, 18)",
        "INSERT INTO products (id, category_id, name, description, price, stock) VALUES (3, 2, 'Desk Lamp', 'Adjustable LED desk lamp', 45.90, 24)",
        "INSERT INTO products (id, category_id, name, description, price, stock) VALUES (4, 3, 'USB-C Dock', 'Multi-port office docking station', 89.50, 11)",
        "INSERT INTO orders (id, customer_id, product_id, quantity, status, total_amount) VALUES (1, 1, 1, 2, 'paid', 79.80)",
        "INSERT INTO orders (id, customer_id, product_id, quantity, status, total_amount) VALUES (2, 2, 2, 1, 'pending', 129.90)",
        "INSERT INTO orders (id, customer_id, product_id, quantity, status, total_amount) VALUES (3, 1, 3, 1, 'paid', 45.90)",
        "INSERT INTO orders (id, customer_id, product_id, quantity, status, total_amount) VALUES (4, 3, 4, 3, 'paid', 268.50)"
    });

    boost::json::object stats;
    stats["categories"] = 3;
    stats["customers"] = 3;
    stats["products"] = 4;
    stats["orders"] = 4;
    return stats;
}

} // namespace dynamic_api_app_demo

class DynamicAppDemoResetHandler : public HandlerBase {
protected:
    void handlePost(
        const http::request<http::string_body>&,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        boost::json::object payload;
        payload["ok"] = true;
        payload["message"] = "Demo database reset";
        payload["removed"] = dynamic_api_app_demo::resetFiles();
        payload["next_step"] = "Click Initialize demo database to recreate demo tables and seed rows.";
        buildJsonResponse(res, http::status::ok, boost::json::serialize(payload));
    }
};

class DynamicAppDemoSetupHandler : public HandlerBase {
protected:
    void handlePost(
        const http::request<http::string_body>&,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        try {
            const auto xml = dynamic_api_app_demo::loadDemoSchemaXml();
            if (xml.empty()) {
                boost::json::object error;
                error["ok"] = false;
                error["message"] = "Bundled demo schema file static/demo_schema.xml was not found";
                buildJsonResponse(res, http::status::internal_server_error, boost::json::serialize(error));
                return;
            }

            boost::json::object payload;
            payload["ok"] = true;
            payload["message"] = "Demo database initialized";
            payload["removed"] = dynamic_api_app_demo::resetFiles();
            dynamic_api_app_demo::writeDemoSchemaCopy(xml);
            payload["uploaded_schema_path"] = dynamic_api_app_runtime::config().uploadedSchemaPath.string();
            payload["database_path"] = dynamic_api_app_runtime::config().databaseConfig.dbname;
            payload["seeded"] = dynamic_api_app_demo::createDemoDatabase();
            boost::json::array tables;
            tables.emplace_back("categories");
            tables.emplace_back("customers");
            tables.emplace_back("products");
            tables.emplace_back("orders");
            payload["tables"] = tables;
            payload["next_step"] = "Open /backend/query-builder, /backend/table or /backend/api-playground. All bundled examples are now runnable.";
            buildJsonResponse(res, http::status::ok, boost::json::serialize(payload));
        } catch (const std::exception& e) {
            boost::json::object error;
            error["ok"] = false;
            error["message"] = std::string("Failed to initialize demo database: ") + e.what();
            buildJsonResponse(res, http::status::internal_server_error, boost::json::serialize(error));
        }
    }
};

inline std::shared_ptr<DynamicAppDemoResetHandler> makeDemoResetHandler() {
    return std::make_shared<DynamicAppDemoResetHandler>();
}

inline std::shared_ptr<DynamicAppDemoSetupHandler> makeDemoSetupHandler() {
    return std::make_shared<DynamicAppDemoSetupHandler>();
}


inline std::shared_ptr<DynamicAppPageHandler> makeReactSpaHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        (dynamic_api_app_paths::frontendDistDir() / "index.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Dynamic React App</title></head><body><h1>Qornix Dynamic React App</h1><p>React frontend build was not found. Run <code>npm install</code> and <code>npm run build</code> in <code>frontend/</code>, or build the project with CMake.</p><p><a href="/backend">Open backend tools</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeDynamicHomeHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("home.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Dynamic API</title></head><body><h1>Qornix Dynamic API</h1><p>Landing page template not found.</p><p><a href="/backend/instructions">Open instructions</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeSchemaManagerPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("schema_manager.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Schema Manager</title></head><body><h1>Qornix Schema Manager</h1><p>Schema Manager template file was not found.</p><p><a href="/backend">Back to backend home</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeInstructionsPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("instructions.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Quick Start</title></head><body><h1>Qornix Quick Start</h1><ol><li>Edit schema/app.schema.xml</li><li>Open /backend/schema-manager</li><li>Validate, diff and apply a plan</li><li>Use /backend/query-builder, /backend/table, /backend/api-playground and /backend/docs</li></ol></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeQueryBuilderPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("query_builder.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Query Builder</title></head><body><h1>Qornix Query Builder</h1><p>Query Builder template file was not found.</p><p><a href="/backend">Back to backend home</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeTableBrowserPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("table.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Table Browser</title></head><body><h1>Qornix Table Browser</h1><p>Table Browser template file was not found.</p><p><a href="/backend">Back to backend home</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeRowViewPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("row_view.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Row View</title></head><body><h1>Qornix Row View</h1><p>Row View template file was not found.</p><p><a href="/backend/table">Back to Table Browser</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeApiPlaygroundPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("api_playground.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix API Playground</title></head><body><h1>Qornix API Playground</h1><p>API Playground template file was not found.</p><p><a href="/backend">Back to backend home</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeDocsPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("docs.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix API Docs</title></head><body><h1>Qornix API Docs</h1><p>Docs template file was not found.</p><p><a href="/api/dynamic/openapi.json">Open raw OpenAPI JSON</a></p></body></html>)HTML"
    );
}
inline std::shared_ptr<DynamicAppPageHandler> makeQuerySyntaxPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("query_syntax.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Query Syntax</title></head><body><h1>Qornix Dynamic API Query Syntax</h1><p>Query syntax documentation template file was not found.</p><p><a href="/backend/docs">Back to docs</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<DynamicAppPageHandler> makeSchemaDocsPageHandler() {
    return std::make_shared<DynamicAppPageHandler>(
        dynamic_api_app_paths::templatePath("schema_docs.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Schema Guide</title></head><body><h1>Qornix Schema Manager and XML Schema Guide</h1><p>Schema documentation template file was not found.</p><p><a href="/backend/docs">Back to docs</a></p></body></html>)HTML"
    );
}
