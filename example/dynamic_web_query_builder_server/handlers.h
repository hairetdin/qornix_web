/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include "demo_database.h"
#include "example_paths.h"
#include "query_builder.h"
#include "schema_loader.h"
#include "template_loader.h"
#include <memory>
#include <algorithm>
#include <cctype>
#include <climits>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <boost/json.hpp>

#include "handler_base.h"
#include <mutex>
#include <chrono>

namespace {
// Table field cache: {table -> {column -> verboseName}}
static std::map<std::string, std::map<std::string, std::string>> schema_cache;
static std::filesystem::file_time_type cache_mtime;
static std::chrono::steady_clock::time_point last_db_validation;
static const auto VALIDATION_INTERVAL = std::chrono::seconds(30);
static std::mutex schema_cache_mutex;

// Check whether the cache needs to be reloaded
static bool needsCacheReload(const std::filesystem::path& schemaPath) {
    if (!std::filesystem::exists(schemaPath)) {
        return true;
    }

    auto current_mtime = std::filesystem::last_write_time(schemaPath);
    if (current_mtime != cache_mtime) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - last_db_validation >= VALIDATION_INTERVAL) {
        return true;
    }

    return false;
}

// Reload the cache from XML and validate it against the database
static void rebuildSchemaCache(
    const std::filesystem::path& schemaPath,
    std::shared_ptr<DatabaseInterface> db
) {
    schema_cache.clear();

    // Load XML
    if (std::filesystem::exists(schemaPath)) {
        try {
            bool loaded = SchemaLoader::loadSchemaFromFile(schemaPath.string());
            if (loaded) {
                const auto& entities = SchemaLoader::getEntities();
                for (const auto& [entityName, entity] : entities) {
                    for (const auto& field : entity.fields) {
                        if (!field.verboseName.empty()) {
                            schema_cache[entityName][field.name] = field.verboseName;
                        }
                    }
                }
                std::cout << "DEBUG: Schema cache rebuilt with " << schema_cache.size() << " tables from XML" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: failed to load schema from XML: " << e.what() << std::endl;
        }
    }

    // Validate against the database: check that all tables from the cache exist in the database
    if (db && db->isConnected()) {
        try {
            auto db_tables = db->getTableNames();
            for (const auto& table : db_tables) {
                if (schema_cache.find(table) == schema_cache.end()) {
                    // Table is not in the cache; add it with auto-generated labels
                    auto fields = db->getTableFields(table);
                    for (const auto& f : fields) {
                        std::string label = f;
                        bool capitalizeNext = true;
                        for (size_t i = 0; i < label.size(); ++i) {
                            if (label[i] == '_') {
                                label[i] = ' ';
                                capitalizeNext = true;
                            } else if (capitalizeNext) {
                                label[i] = std::toupper(static_cast<unsigned char>(label[i]));
                                capitalizeNext = false;
                            }
                        }
                        schema_cache[table][f] = label;
                    }
                    std::cout << "DEBUG: Added table '" << table << "' to cache (auto-generated labels)" << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: DB validation failed: " << e.what() << std::endl;
        }
    }

    // Update cache metadata
    cache_mtime = std::filesystem::last_write_time(schemaPath);
    last_db_validation = std::chrono::steady_clock::now();
}

// Get labels for the table
static std::map<std::string, std::string> getTableLabels(
    const std::string& table,
    const std::filesystem::path& schemaPath,
    std::shared_ptr<DatabaseInterface> db
) {
    std::lock_guard<std::mutex> lock(schema_cache_mutex);

    if (needsCacheReload(schemaPath)) {
        rebuildSchemaCache(schemaPath, db);
    }

    auto it = schema_cache.find(table);
    if (it != schema_cache.end()) {
        return it->second;
    }

    // If the table is not in the cache, generate labels automatically
    std::map<std::string, std::string> labels;
    if (db && db->isConnected()) {
        try {
            auto fields = db->getTableFields(table);
            for (const auto& f : fields) {
                std::string label = f;
                bool capitalizeNext = true;
                for (size_t i = 0; i < label.size(); ++i) {
                    if (label[i] == '_') {
                        label[i] = ' ';
                        capitalizeNext = true;
                    } else if (capitalizeNext) {
                        label[i] = std::toupper(static_cast<unsigned char>(label[i]));
                        capitalizeNext = false;
                    }
                }
                labels[f] = label;
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: failed to get table fields: " << e.what() << std::endl;
        }
    }

    return labels;
}

std::unique_ptr<DatabaseInterface> initDynamicDbInterface() {
    try {
        auto db = DatabaseInterface::init(dynamic_web_query_builder_example::demoDatabaseConfig());
        std::cout << "Successfully initialized demo SQLite metadata connection" << std::endl;
        return db;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize demo SQLite metadata connection: " << e.what() << std::endl;
        throw std::runtime_error("ORM initialization failed");
    }
}

std::unique_ptr<QueryBuilder> initDynamicQueryBuilder() {
    try {
        // Each thread gets its own database connection — SQLite does NOT support
        // concurrent access from multiple threads to the same sqlite3* handle.
        auto db = DatabaseInterface::init(dynamic_web_query_builder_example::demoDatabaseConfig());
        std::shared_ptr<DatabaseInterface> sharedDb(std::move(db));
        auto queryBuilder = std::make_unique<QueryBuilder>();
        queryBuilder->setDatabaseInterface(sharedDb);
        return queryBuilder;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize demo SQLite QueryBuilder: " << e.what() << std::endl;
        throw std::runtime_error("QueryBuilder initialization failed");
    }
}

bool startsWithCaseInsensitive(const std::string& value, const std::string& prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (value.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::string getQueryParam(const urls::url_view& url, const std::string& key) {
    for (const auto& p : url.params()) {
        if (std::string(p.key) == key) {
            return std::string(p.value);
        }
    }
    return "";
}

int getQueryIntParam(const urls::url_view& url, const std::string& key, int defaultValue, int minValue, int maxValue) {
    const std::string raw = getQueryParam(url, key);
    if (raw.empty()) {
        return defaultValue;
    }

    try {
        long long parsed = std::stoll(raw);
        if (parsed < minValue) {
            return minValue;
        }
        if (parsed > maxValue) {
            return maxValue;
        }
        return static_cast<int>(parsed);
    } catch (...) {
        return defaultValue;
    }
}
} // namespace


class QueryBuilderHandler: public HandlerBase {
private:
    QueryBuilder& queryBuilderForCurrentThread() const {
        static thread_local std::unique_ptr<QueryBuilder> query_builder = initDynamicQueryBuilder();
        return *query_builder;
    }

public:
    QueryBuilderHandler() = default;
protected:
    void handleGet(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url,
        const std::map<std::string, std::string> &params
    ) override {
        try {
            // Check whether the table and id parameters are specified in the URL
            std::string table = params.count("table") ? params.at("table") : "";
            std::string id = params.count("id") ? params.at("id") : "";

            auto& query_builder = queryBuilderForCurrentThread();

            // Pass the ?query={...} request to QueryBuilder
            query_builder.parseRequest(std::string(url.query()), table, "GET");

            // Explicitly set the table if it is specified in the parameters
            if (!table.empty()) {
                query_builder.setTable(table);
            }

            // If an ID is specified, add a filter
            if (!id.empty()) {
                query_builder.addFilter("id=" + id);
            }

            // Use the new exec method instead of getResponse
            ResponseData builder_response = query_builder.exec();

            // Use the helper method from HandlerBase
            buildJsonResponse(res, builder_response.status, boost::json::serialize(builder_response.data),
                builder_response.message);

        } catch (const std::exception &e) {
            buildTextResponse(res, http::status::internal_server_error, e.what());
        }
    }

    void handlePost(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url,
        const std::map<std::string, std::string> &params
    ) override {
        try {
            std::string table = params.count("table") ? params.at("table") : "";
            std::string id = params.count("id") ? params.at("id") : "";

            // Use the prepared request body for the POST request
            const std::string& json_request = req.body();

            auto& query_builder = queryBuilderForCurrentThread();
            query_builder.parseRequest(std::string(json_request), table, "POST");
            // If an ID is specified, add a filter
            if (!id.empty()) {
                query_builder.addFilter("id="+id);
            }

            auto builder_response = query_builder.getResponse();

            buildJsonResponse(res, builder_response.status, boost::json::serialize(builder_response.data),
                builder_response.message);

        } catch (const std::exception &e) {
            buildTextResponse(res, http::status::internal_server_error, e.what());
        }
    }

    void handlePatch(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url,
        const std::map<std::string, std::string> &params
    ) override {
        try {
            std::string table = params.count("table") ? params.at("table") : "";
            std::string id = params.count("id") ? params.at("id") : "";

            // Use the prepared request body for the PATCH request
            const std::string& json_request = req.body();

            auto& query_builder = queryBuilderForCurrentThread();
            query_builder.parseRequest(json_request, table, "PATCH");
            if (!id.empty()) {
                query_builder.addFilter("id="+id);
            }
            auto builder_response = query_builder.getResponse();

            buildJsonResponse(res, builder_response.status, boost::json::serialize(builder_response.data),
                builder_response.message);

        } catch (const std::exception &e) {
            buildTextResponse(res, http::status::internal_server_error, e.what());
        }
    }

    void handlePut(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url,
        const std::map<std::string, std::string> &params
    ) override {
        try {
            std::string table = params.count("table") ? params.at("table") : "";
            std::string id = params.count("id") ? params.at("id") : "";

            // Use the prepared request body for the PUT request
            const std::string& json_request = req.body();

            auto& query_builder = queryBuilderForCurrentThread();
            query_builder.parseRequest(json_request, table, "PUT");
            if (!id.empty()) {
                query_builder.addFilter("id="+id);
            }
            auto builder_response = query_builder.getResponse();

            buildJsonResponse(res, builder_response.status, boost::json::serialize(builder_response.data),
                builder_response.message);

        } catch (const std::exception &e) {
            buildTextResponse(res, http::status::internal_server_error, e.what());
        }
    }

    void handleDelete(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url,
        const std::map<std::string, std::string> &params
    ) override {
        try {
            std::string table = params.count("table") ? params.at("table") : "";
            std::string id = params.count("id") ? params.at("id") : "";

            // Use the prepared request body for the DELETE request
            const std::string& json_request = req.body();

            auto& query_builder = queryBuilderForCurrentThread();
            query_builder.parseRequest(json_request, table, "DELETE");
            if (!id.empty()) {
                query_builder.addFilter("id="+id);
            }
            auto builder_response = query_builder.getResponse();

            buildJsonResponse(res, builder_response.status, boost::json::serialize(builder_response.data),
                builder_response.message);

        } catch (const std::exception &e) {
            buildTextResponse(res, http::status::internal_server_error, e.what());
        }
    }
};

// Handler for exporting the XML schema of the current demo SQLite database
class SchemaExportHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>& params
    ) override {
        try {
            const auto schemaPath = dynamic_web_query_builder_example::demoSchemaPath();
            if (!schemaPath.parent_path().empty()) {
                std::filesystem::create_directories(schemaPath.parent_path());
            }

            auto db = DatabaseInterface::init(dynamic_web_query_builder_example::demoDatabaseConfig());
            std::shared_ptr<DatabaseInterface> sharedDb(std::move(db));

            if (!SchemaLoader::saveSchemaFromDatabase(sharedDb, schemaPath.string())) {
                buildErrorResponse(res, http::status::internal_server_error, "Failed to generate XML schema");
                return;
            }

            if (!std::filesystem::exists(schemaPath) || std::filesystem::file_size(schemaPath) == 0) {
                buildErrorResponse(res, http::status::internal_server_error, "Generated XML schema file is missing or empty");
                return;
            }

            std::ifstream schemaFile(schemaPath);
            if (!schemaFile) {
                buildErrorResponse(res, http::status::internal_server_error, "Failed to read generated XML schema");
                return;
            }

            std::ostringstream buffer;
            buffer << schemaFile.rdbuf();

            res.result(http::status::ok);
            res.set(http::field::content_type, "application/xml; charset=utf-8");
            res.set(http::field::content_disposition, "inline; filename=\"dynamic_web_query_builder_demo.schema.xml\"");
            res.set("X-Schema-Path", schemaPath.string());
            res.body() = buffer.str();
            res.prepare_payload();
        } catch (const std::exception& e) {
            buildErrorResponse(res, http::status::internal_server_error, e.what());
        }
    }
};

// Handler for applying the XML schema to the current demo SQLite database
class SchemaApplyHandler : public HandlerBase {
protected:
    void handlePost(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>& params
    ) override {
        try {
            const std::string xmlBody = req.body();
            if (xmlBody.empty()) {
                buildErrorResponse(res, http::status::bad_request, "XML schema body is empty");
                return;
            }

            const auto schemaPath = dynamic_web_query_builder_example::uploadedSchemaPath();
            if (!schemaPath.parent_path().empty()) {
                std::filesystem::create_directories(schemaPath.parent_path());
            }

            std::ofstream schemaFile(schemaPath, std::ios::binary | std::ios::trunc);
            if (!schemaFile) {
                buildErrorResponse(res, http::status::internal_server_error, "Failed to open uploaded schema file for writing");
                return;
            }

            schemaFile << xmlBody;
            schemaFile.close();

            if (!SchemaLoader::loadSchemaFromFile(schemaPath.string())) {
                buildErrorResponse(res, http::status::bad_request, "Failed to load XML schema");
                return;
            }

            auto db = DatabaseInterface::init(dynamic_web_query_builder_example::demoDatabaseConfig());
            std::shared_ptr<DatabaseInterface> sharedDb(std::move(db));

            if (!SchemaLoader::loadSchemaFromDB(sharedDb)) {
                buildErrorResponse(res, http::status::internal_server_error, "Failed to load current database schema");
                return;
            }

            if (!SchemaLoader::applySchemaChangesToDB(sharedDb)) {
                buildErrorResponse(res, http::status::internal_server_error, "Failed to apply XML schema to database");
                return;
            }

            boost::json::object payload;
            payload["applied"] = true;
            payload["schema_path"] = schemaPath.string();
            payload["database_path"] = dynamic_web_query_builder_example::demoDatabasePath().string();
            buildJsonResponse(res, http::status::ok, boost::json::serialize(payload), "Schema applied");
        } catch (const std::exception& e) {
            buildErrorResponse(res, http::status::internal_server_error, e.what());
        }
    }
};


// Handler for the root page and API description
class ApiDescriptionHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>& params
    ) override {
        try {
            std::string home_html = TemplateLoader::loadFile("home.html", dynamic_web_query_builder_example::templatesDir());

            res.result(http::status::ok);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = home_html;
            res.prepare_payload();

        } catch (const std::exception& e) {
            // Show a 500 error if template loading fails
            std::string error_html = TemplateLoader::load500Template();
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = error_html;
            res.prepare_payload();
        }
    }
};

// Handler for the XML schema management page
class SchemaManagerPageHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>& params
    ) override {
        try {
            std::string schema_html = TemplateLoader::loadFile("schema_manager.html", dynamic_web_query_builder_example::templatesDir());

            res.result(http::status::ok);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = schema_html;
            res.prepare_payload();

        } catch (const std::exception& e) {
            std::string error_html = TemplateLoader::load500Template();
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = error_html;
            res.prepare_payload();
        }
    }
};

// Handler for the Query Builder page
class QueryBuilderInterfaceHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>& params
    ) override {
        try {
            std::string qb_html = TemplateLoader::loadFile("query_builder.html", dynamic_web_query_builder_example::templatesDir());

            res.result(http::status::ok);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = qb_html;
            res.prepare_payload();

        } catch (const std::exception& e) {
            std::string error_html = TemplateLoader::load500Template();
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = error_html;
            res.prepare_payload();
        }
    }
};

// Handler for the Table Browser page
class TableInterfaceHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>& params
    ) override {
        try {
            std::string table_html = TemplateLoader::loadFile("table.html", dynamic_web_query_builder_example::templatesDir());

            res.result(http::status::ok);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = table_html;
            res.prepare_payload();

        } catch (const std::exception& e) {
            std::string error_html = TemplateLoader::load500Template();
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = error_html;
            res.prepare_payload();
        }
    }
};

// Handler for the Row View page (detailed row view)
class RowViewInterfaceHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>& params
    ) override {
        try {
            std::string row_view_html = TemplateLoader::loadFile("row_view.html", dynamic_web_query_builder_example::templatesDir());

            res.result(http::status::ok);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = row_view_html;
            res.prepare_payload();

        } catch (const std::exception& e) {
            std::string error_html = TemplateLoader::load500Template();
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = error_html;
            res.prepare_payload();
        }
    }
};

// Schema metadata + autocomplete
class QueryMetadataHandler : public HandlerBase {
private:
    std::shared_ptr<DatabaseInterface> db_;

public:
    QueryMetadataHandler() {
        // Each handler instance gets its own connection — SQLite does NOT support
        // concurrent access from multiple threads to the same sqlite3* handle.
        auto db = DatabaseInterface::init(dynamic_web_query_builder_example::demoDatabaseConfig());
        db_ = std::shared_ptr<DatabaseInterface>(
            db.release(),
            [](DatabaseInterface* ptr) { delete ptr; }
        );
    }

protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>& params
    ) override {
        try {
            if (!db_ || !db_->isConnected()) {
                buildErrorResponse(res, http::status::service_unavailable, "Database is not connected");
                return;
            }

            // /api/dynamic/meta/tables
            if (params.count("entity") && params.at("entity") == "tables") {
                std::string q = getQueryParam(url, "q");
                const int limit = getQueryIntParam(url, "limit", INT_MAX, 1, INT_MAX);
                const int offset = getQueryIntParam(url, "offset", 0, 0, INT_MAX);
                auto tables = db_->getTableNames();
                boost::json::array arr;
                int skipped = 0;
                for (const auto& t : tables) {
                    if (!startsWithCaseInsensitive(t, q)) {
                        continue;
                    }
                    if (skipped < offset) {
                        ++skipped;
                        continue;
                    }
                    if (static_cast<int>(arr.size()) >= limit) {
                        break;
                    }
                    arr.emplace_back(t);
                }
                buildJsonResponse(res, http::status::ok, boost::json::serialize(arr), "Tables loaded");
                return;
            }

            // /api/dynamic/meta/{table}/fields
            if (params.count("table") && params.count("entity") && params.at("entity") == "fields") {
                const std::string table = params.at("table");
                std::string q = getQueryParam(url, "q");
                auto fields = db_->getTableFields(table);
                boost::json::array arr;
                for (const auto& f : fields) {
                    if (startsWithCaseInsensitive(f, q)) {
                        arr.emplace_back(f);
                    }
                }
                buildJsonResponse(res, http::status::ok, boost::json::serialize(arr), "Fields loaded");
                return;
            }

           // /api/dynamic/meta/{table}/fields-info — returns field metadata with verbose names/labels (cached)
            if (params.count("table") && params.count("entity") && params.at("entity") == "fields-info") {
                const std::string table = params.at("table");
                auto fields = db_->getTableFields(table);

                const auto schemaPath = dynamic_web_query_builder_example::demoSchemaPath();
                auto fieldLabels = getTableLabels(table, schemaPath, db_);

                boost::json::array arr;
                for (const auto& f : fields) {
                    boost::json::object fieldObj;
                    fieldObj["name"] = f;
                    auto labelIt = fieldLabels.find(f);
                    fieldObj["verboseName"] = labelIt != fieldLabels.end() ? labelIt->second : f;
                    arr.emplace_back(fieldObj);
                }
                buildJsonResponse(res, http::status::ok, boost::json::serialize(arr), "Fields info loaded");
                return;
            }

            // /api/dynamic/meta/autocomplete?type=tables|fields&table=...&q=...
            if (params.count("entity") && params.at("entity") == "autocomplete") {
                const std::string type = getQueryParam(url, "type");
                const std::string table = getQueryParam(url, "table");
                const std::string q = getQueryParam(url, "q");

                boost::json::array items;
                if (type == "tables") {
                    auto tables = db_->getTableNames();
                    for (const auto& t : tables) {
                        if (startsWithCaseInsensitive(t, q)) {
                            items.emplace_back(t);
                        }
                    }
                } else if (type == "fields") {
                    if (table.empty()) {
                        buildErrorResponse(res, http::status::bad_request, "Parameter 'table' is required for fields autocomplete");
                        return;
                    }
                    auto fields = db_->getTableFields(table);
                    for (const auto& f : fields) {
                        if (startsWithCaseInsensitive(f, q)) {
                            items.emplace_back(f);
                        }
                    }
                } else {
                    buildErrorResponse(res, http::status::bad_request, "Parameter 'type' must be 'tables' or 'fields'");
                    return;
                }

                boost::json::object payload;
                payload["type"] = type;
                payload["query"] = q;
                payload["items"] = items;
                payload["count"] = static_cast<int>(items.size());

                buildJsonResponse(res, http::status::ok, boost::json::serialize(payload), "Autocomplete loaded");
                return;
            }

            buildErrorResponse(res, http::status::not_found, "Metadata endpoint not found");
        } catch (const std::exception& e) {
            buildErrorResponse(res, http::status::internal_server_error, e.what());
        }
    }
};
