/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "config.h"
#include "config_parser.h"
#include "database_interface.h"
#include "handler_base.h"
#include "query_builder.h"
#include "schema_loader.h"

#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wiki_example {
namespace json = boost::json;

// ============================================================================
// WikiConfig — all settings loaded from config.yaml
// ============================================================================

struct WikiConfig {
    // Server
    std::string server_address = "127.0.0.1";
    unsigned short server_port = 8008;

    // Database
    std::string db_driver = "sqlite";
    std::string db_path = "./data/wiki.sqlite";
    std::string db_schema = "wiki_schema.xml";

    // Auth
    std::string auth_jwt_secret = "qornix-wiki-secret-key-change-in-production";
    int auth_session_duration_minutes = 30;
    int auth_jwt_duration_minutes = 60;
    bool auth_password_validation = true;
    size_t auth_min_password_length = 6;

    // Logging
    bool logging_enabled = false;
    std::string logging_level = "debug";
    bool logging_to_file = true;
    std::string logging_file_path = "./logs/wiki_server";
    size_t logging_rotation_size = 10 * 1024 * 1024; // 10 MB
    size_t logging_max_files = 5;
};

inline WikiConfig loadWikiConfig(const std::string& configPath = "config.yaml") {
    WikiConfig config;
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return config;
    }
    file.close();

    try {
        YAML::Node node = YAML::LoadFile(configPath);

        if (node["server"]) {
            const auto& s = node["server"];
            config.server_address = s["address"].as<std::string>(config.server_address);
            config.server_port = s["port"].as<unsigned short>(config.server_port);
        }

        if (node["database"]) {
            const auto& d = node["database"];
            config.db_driver = d["driver"].as<std::string>(config.db_driver);
            config.db_path = d["path"].as<std::string>(config.db_path);
            config.db_schema = d["schema"].as<std::string>(config.db_schema);
        }

        if (node["auth"]) {
            const auto& a = node["auth"];
            config.auth_jwt_secret = a["jwt_secret"].as<std::string>(config.auth_jwt_secret);
            config.auth_session_duration_minutes = a["session_duration_minutes"].as<int>(config.auth_session_duration_minutes);
            config.auth_jwt_duration_minutes = a["jwt_duration_minutes"].as<int>(config.auth_jwt_duration_minutes);
            config.auth_password_validation = a["password_validation"].as<bool>(config.auth_password_validation);
            config.auth_min_password_length = a["min_password_length"].as<size_t>(config.auth_min_password_length);
        }

        if (node["logging"]) {
            const auto& l = node["logging"];
            config.logging_enabled = l["enabled"].as<bool>(config.logging_enabled);
            config.logging_level = l["level"].as<std::string>(config.logging_level);
            config.logging_to_file = l["to_file"].as<bool>(config.logging_to_file);
            config.logging_file_path = l["file_path"].as<std::string>(config.logging_file_path);
            config.logging_rotation_size = l["rotation_size"].as<size_t>(config.logging_rotation_size);
            config.logging_max_files = l["max_files"].as<size_t>(config.logging_max_files);
        }
    } catch (const std::exception& e) {
        // Fall back to defaults on parse error.
        // Config loaded will have default values.
    }

    return config;
}

inline LogLevel stringToLogLevel(const std::string& level) {
    if (level == "trace") return LogLevel::trace;
    if (level == "debug") return LogLevel::debug;
    if (level == "info") return LogLevel::info;
    if (level == "warning") return LogLevel::warning;
    if (level == "error") return LogLevel::error;
    if (level == "fatal") return LogLevel::fatal;
    return LogLevel::debug;
}

inline boost::log::trivial::severity_level toBoostLogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::trace:   return boost::log::trivial::trace;
        case LogLevel::debug:   return boost::log::trivial::debug;
        case LogLevel::info:    return boost::log::trivial::info;
        case LogLevel::warning: return boost::log::trivial::warning;
        case LogLevel::error:   return boost::log::trivial::error;
        case LogLevel::fatal:   return boost::log::trivial::fatal;
        default:                return boost::log::trivial::debug;
    }
}

inline std::string sourceDirectory() {
#ifdef WIKI_EXAMPLE_SOURCE_DIR
    return WIKI_EXAMPLE_SOURCE_DIR;
#else
    return ".";
#endif
}

inline std::string templatePath() {
    return sourceDirectory() + "/templates/wiki.html";
}

inline std::string detailTemplatePath() {
    return sourceDirectory() + "/templates/wiki_detail.html";
}

inline std::string staticDirectory() {
    return sourceDirectory() + "/static";
}

inline std::string dataDirectory() {
    return sourceDirectory() + "/data";
}

inline std::string databasePath(const WikiConfig& config = WikiConfig{}) {
    // If db_path is absolute, use it directly; otherwise prepend source directory
    if (std::filesystem::path(config.db_path).is_absolute()) {
        return config.db_path;
    }
    return config.db_path;
}

inline std::string schemaPath(const WikiConfig& config = WikiConfig{}) {
    // If schema path is absolute, use it directly; otherwise prepend source directory
    if (std::filesystem::path(config.db_schema).is_absolute()) {
        return config.db_schema;
    }
    return sourceDirectory() + "/" + config.db_schema;
}

inline DatabaseConfig databaseConfig(const WikiConfig& config = WikiConfig{}) {
    std::filesystem::create_directories(std::filesystem::path(config.db_path).parent_path());

    DatabaseConfig dbConfig{};
    dbConfig.driver = config.db_driver;
    dbConfig.dbname = databasePath(config);
    dbConfig.connectionString = databasePath(config);
    return dbConfig;
}

struct WikiArticle {
    int id = 0;
    std::string title;
    std::string slug;
    std::string summary;
    std::string content;
    std::string category;
    std::string status;
    std::string author;
    std::string tags;
    std::string templateName;
    int readingTime = 4;
    bool favorite = false;
    int views = 0;
    std::string createdAt;
    std::string updatedAt;
};

struct WikiFilters {
    std::string query;
    std::string category;
    std::string status;
    bool favoritesOnly = false;
    bool recentOnly = false;
    int limit = 0;
};

struct ArticleInput {
    std::string title;
    std::string summary;
    std::string content;
    std::string category;
    std::string status;
    std::string author;
    std::string tags;
    std::string templateName;
    int readingTime = 4;
};

inline std::string trim(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }

    return std::string(begin, end);
}

inline std::string nowTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

inline std::string daysAgoTimestamp(int days) {
    const auto point = std::chrono::system_clock::now() - std::chrono::hours(24 * days);
    const auto time = std::chrono::system_clock::to_time_t(point);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

inline std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline bool containsSearchText(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }

    if (haystack.find(needle) != std::string::npos) {
        return true;
    }

    return toLowerAscii(haystack).find(toLowerAscii(needle)) != std::string::npos;
}

inline std::vector<std::string> splitTags(const std::string& tags) {
    std::vector<std::string> result;
    std::stringstream stream(tags);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

inline std::string joinTags(const std::vector<std::string>& tags) {
    std::string result;
    for (const auto& tag : tags) {
        const auto cleaned = trim(tag);
        if (cleaned.empty()) {
            continue;
        }
        if (!result.empty()) {
            result += ", ";
        }
        result += cleaned;
    }
    return result;
}

inline std::string normalizeTags(const std::string& tags) {
    return joinTags(splitTags(tags));
}

inline std::string makeSlug(const std::string& title) {
    std::string slug;
    bool previousDash = false;
    for (const unsigned char ch : title) {
        if (std::isalnum(ch)) {
            slug.push_back(static_cast<char>(std::tolower(ch)));
            previousDash = false;
        } else if (std::isspace(ch) || ch == '-' || ch == '_') {
            if (!previousDash && !slug.empty()) {
                slug.push_back('-');
                previousDash = true;
            }
        }
    }

    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    if (slug.empty()) {
        slug = "article";
    }

    const auto epoch = std::chrono::system_clock::now().time_since_epoch();
    const auto suffix = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
    return slug + "-" + std::to_string(suffix);
}

inline std::string jsonValueAsString(const json::value& value) {
    if (value.is_string()) return std::string(value.as_string().c_str());
    if (value.is_int64()) return std::to_string(value.as_int64());
    if (value.is_uint64()) return std::to_string(value.as_uint64());
    if (value.is_double()) return std::to_string(value.as_double());
    if (value.is_bool()) return value.as_bool() ? "1" : "0";
    return "";
}

inline std::string objectString(const json::object& object,
                                const std::string& key,
                                const std::string& fallback = "") {
    const auto it = object.find(key);
    if (it == object.end() || it->value().is_null()) {
        return fallback;
    }
    return jsonValueAsString(it->value());
}

inline int objectInt(const json::object& object,
                     const std::string& key,
                     int fallback = 0) {
    const auto raw = objectString(object, key);
    if (raw.empty() || raw == "NULL") {
        return fallback;
    }
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

inline bool objectBool(const json::object& object,
                       const std::string& key,
                       bool fallback = false) {
    const auto raw = toLowerAscii(objectString(object, key));
    if (raw.empty() || raw == "NULL") {
        return fallback;
    }
    return raw == "1" || raw == "true" || raw == "yes";
}

inline std::string getJsonString(const json::object& object,
                                 const std::string& key,
                                 const std::string& fallback = "") {
    const auto it = object.find(key);
    if (it == object.end() || it->value().is_null() || !it->value().is_string()) {
        return fallback;
    }
    return std::string(it->value().as_string().c_str());
}

inline int getJsonInt(const json::object& object,
                      const std::string& key,
                      int fallback,
                      int minValue,
                      int maxValue) {
    const auto it = object.find(key);
    if (it == object.end() || it->value().is_null()) {
        return fallback;
    }

    int value = fallback;
    if (it->value().is_int64()) {
        value = static_cast<int>(it->value().as_int64());
    } else if (it->value().is_uint64()) {
        value = static_cast<int>(it->value().as_uint64());
    } else if (it->value().is_double()) {
        value = static_cast<int>(it->value().as_double());
    }

    return std::max(minValue, std::min(maxValue, value));
}

inline std::string tagsFromJson(const json::object& object) {
    const auto it = object.find("tags");
    if (it == object.end() || it->value().is_null()) {
        return "";
    }

    if (it->value().is_string()) {
        return normalizeTags(std::string(it->value().as_string().c_str()));
    }

    if (!it->value().is_array()) {
        return "";
    }

    std::vector<std::string> tags;
    for (const auto& item : it->value().as_array()) {
        if (item.is_string()) {
            tags.push_back(std::string(item.as_string().c_str()));
        }
    }
    return joinTags(tags);
}

inline ArticleInput parseArticleInput(const std::string& body) {
    json::value parsed;
    try {
        parsed = json::parse(body);
    } catch (const std::exception& e) {
        throw std::invalid_argument("Invalid JSON: " + std::string(e.what()));
    }

    if (!parsed.is_object()) {
        throw std::invalid_argument("Request body must be a JSON object");
    }

    const auto& object = parsed.as_object();
    ArticleInput input;
    input.title = trim(getJsonString(object, "title"));
    input.summary = trim(getJsonString(object, "summary"));
    input.content = trim(getJsonString(object, "content"));
    input.category = trim(getJsonString(object, "category", "General"));
    input.status = trim(getJsonString(object, "status", "draft"));
    input.author = trim(getJsonString(object, "author", "Wiki Team"));
    input.tags = tagsFromJson(object);
    input.templateName = trim(getJsonString(object, "template", "Guide"));
    input.readingTime = getJsonInt(object, "readingTime", 4, 1, 60);

    if (input.title.empty()) {
        throw std::invalid_argument("Title is required");
    }
    if (input.summary.empty()) {
        input.summary = input.title;
    }
    if (input.content.empty()) {
        throw std::invalid_argument("Content is required");
    }
    if (input.category.empty()) {
        input.category = "General";
    }
    if (input.author.empty()) {
        input.author = "Wiki Team";
    }
    if (input.templateName.empty()) {
        input.templateName = "Guide";
    }
    if (input.status != "published" && input.status != "draft") {
        input.status = "draft";
    }

    return input;
}

inline json::array tagsToJson(const std::string& tags) {
    json::array result;
    for (const auto& tag : splitTags(tags)) {
        result.emplace_back(tag);
    }
    return result;
}

inline json::object articleToJson(const WikiArticle& article) {
    json::object object;
    object["id"] = article.id;
    object["title"] = article.title;
    object["slug"] = article.slug;
    object["summary"] = article.summary;
    object["content"] = article.content;
    object["category"] = article.category;
    object["status"] = article.status;
    object["author"] = article.author;
    object["tags"] = tagsToJson(article.tags);
    object["template"] = article.templateName;
    object["readingTime"] = article.readingTime;
    object["favorite"] = article.favorite;
    object["views"] = article.views;
    object["createdAt"] = article.createdAt;
    object["updatedAt"] = article.updatedAt;
    return object;
}

class WikiRepository {
public:
    explicit WikiRepository(const WikiConfig& config = WikiConfig{})
        : config_(config) {
        auto db = DatabaseInterface::init(databaseConfig(config_));
        db_ = std::shared_ptr<DatabaseInterface>(std::move(db));
        ensureSchema();
        seedIfEmpty();
    }

    std::vector<WikiArticle> listArticles(const WikiFilters& filters) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto articles = allArticlesLocked();
        const auto recentThreshold = daysAgoTimestamp(14);

        articles.erase(std::remove_if(articles.begin(), articles.end(), [&](const WikiArticle& article) {
            if (!filters.query.empty()) {
                const std::string text = article.title + " " + article.summary + " " + article.content +
                                         " " + article.tags + " " + article.author;
                if (!containsSearchText(text, filters.query)) {
                    return true;
                }
            }
            if (!filters.category.empty() && filters.category != "all" && article.category != filters.category) {
                return true;
            }
            if (!filters.status.empty() && filters.status != "all" && article.status != filters.status) {
                return true;
            }
            if (filters.favoritesOnly && !article.favorite) {
                return true;
            }
            if (filters.recentOnly && article.updatedAt < recentThreshold) {
                return true;
            }
            return false;
        }), articles.end());

        sortArticles(articles);
        if (filters.limit > 0 && static_cast<int>(articles.size()) > filters.limit) {
            articles.resize(filters.limit);
        }
        return articles;
    }

    std::optional<WikiArticle> getArticle(int id, bool incrementViews = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto article = getArticleLocked(id);
        if (!article) {
            return std::nullopt;
        }

        if (incrementViews) {
            json::object data;
            data["views"] = article->views + 1;
            updateByIdLocked("wiki_articles", id, data);
            article = getArticleLocked(id);
        }

        return article;
    }

    WikiArticle createArticle(const ArticleInput& input) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto article = insertArticleLocked(input, nowTimestamp(), nowTimestamp(), false, 0);
        addActivityLocked(article.id, input.author, "создал(а) статью", input.title);
        return article;
    }

    std::optional<WikiArticle> updateArticle(int id, const ArticleInput& input) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!getArticleLocked(id)) {
            return std::nullopt;
        }

        json::object data = articleData(input);
        data["updated_at"] = nowTimestamp();
        updateByIdLocked("wiki_articles", id, data);
        addActivityLocked(id, input.author, "обновил(а) статью", input.title);
        return getArticleLocked(id);
    }

    bool deleteArticle(int id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto article = getArticleLocked(id);
        if (!article) {
            return false;
        }

        addActivityLocked(id, article->author, "удалил(а) статью", article->title);
        QueryBuilder builder = makeBuilder("DELETE", "wiki_articles");
        builder.addFilter("id=" + std::to_string(id));
        const auto response = builder.exec();
        return response.status == http::status::ok;
    }

    std::optional<WikiArticle> toggleFavorite(int id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto article = getArticleLocked(id);
        if (!article) {
            return std::nullopt;
        }

        json::object data;
        data["favorite"] = article->favorite ? 0 : 1;
        updateByIdLocked("wiki_articles", id, data);

        const auto updated = getArticleLocked(id);
        if (updated) {
            addActivityLocked(id, updated->author,
                              updated->favorite ? "добавил(а) в избранное" : "убрал(а) из избранного",
                              updated->title);
        }
        return updated;
    }

    json::object stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto articles = allArticlesLocked();
        std::set<std::string> authors;

        int drafts = 0;
        int published = 0;
        int favorites = 0;
        int recent = 0;
        int fresh = 0;
        const auto recentThreshold = daysAgoTimestamp(14);
        const auto freshThreshold = daysAgoTimestamp(45);

        for (const auto& article : articles) {
            authors.insert(article.author);
            drafts += article.status == "draft" ? 1 : 0;
            published += article.status == "published" ? 1 : 0;
            favorites += article.favorite ? 1 : 0;
            recent += article.updatedAt >= recentThreshold ? 1 : 0;
            fresh += article.updatedAt >= freshThreshold ? 1 : 0;
        }

        json::object result;
        result["articles"] = static_cast<int>(articles.size());
        result["published"] = published;
        result["drafts"] = drafts;
        result["favorites"] = favorites;
        result["authors"] = static_cast<int>(authors.size());
        result["recent"] = recent;
        result["freshness"] = articles.empty() ? 100 : static_cast<int>((fresh * 100.0) / articles.size());
        return result;
    }

    json::array categories() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, int> counts;
        for (const auto& article : allArticlesLocked()) {
            counts[article.category]++;
        }

        std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });

        json::array result;
        for (const auto& [name, count] : sorted) {
            json::object item;
            item["name"] = name;
            item["count"] = count;
            result.emplace_back(item);
        }
        return result;
    }

    json::array activity(int limit = 12) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto rows = allRowsLocked("wiki_activity");
        std::sort(rows.begin(), rows.end(), [](const json::object& lhs, const json::object& rhs) {
            const auto leftCreated = objectString(lhs, "created_at");
            const auto rightCreated = objectString(rhs, "created_at");
            if (leftCreated != rightCreated) {
                return leftCreated > rightCreated;
            }
            return objectInt(lhs, "id") > objectInt(rhs, "id");
        });

        json::array result;
        int added = 0;
        for (const auto& row : rows) {
            if (added++ >= limit) {
                break;
            }

            json::object item;
            item["id"] = objectInt(row, "id");
            item["articleId"] = objectInt(row, "article_id");
            item["actor"] = objectString(row, "actor");
            item["action"] = objectString(row, "action");
            item["detail"] = objectString(row, "detail");
            item["createdAt"] = objectString(row, "created_at");
            result.emplace_back(item);
        }
        return result;
    }

    std::string path() const {
        return databasePath(config_);
    }

private:
    WikiConfig config_;
    std::shared_ptr<DatabaseInterface> db_;
    mutable std::mutex mutex_;

    QueryBuilder makeBuilder(const std::string& method, const std::string& table) {
        QueryBuilder builder;
        builder.setDatabaseInterface(db_);
        builder.setMethod(method).setTable(table);
        return builder;
    }

    void ensureSchema() {
        if (!SchemaLoader::loadSchemaFromFile(schemaPath(config_))) {
            throw std::runtime_error("Failed to load wiki ORM schema: " + schemaPath(config_));
        }

        // SchemaLoader owns schema diffing/table creation. We only fall back to
        // createDBTable when the DB is empty and a driver cannot build dbEntities.
        SchemaLoader::loadSchemaFromDB(db_);
        if (!hasRequiredTables() && !SchemaLoader::applySchemaChangesToDB(db_)) {
            for (const auto& [_, entity] : SchemaLoader::getEntities()) {
                if (!tableExists(entity.tableName)) {
                    SchemaLoader::createDBTable(db_, entity);
                }
            }
        }

        if (!hasRequiredTables()) {
            for (const auto& [_, entity] : SchemaLoader::getEntities()) {
                if (!tableExists(entity.tableName)) {
                    SchemaLoader::createDBTable(db_, entity);
                }
            }
        }

        if (!hasRequiredTables()) {
            throw std::runtime_error("Wiki ORM schema was not applied to SQLite database");
        }
    }

    bool tableExists(const std::string& tableName) {
        const auto tables = db_->getTableNames();
        return std::find(tables.begin(), tables.end(), tableName) != tables.end();
    }

    bool hasRequiredTables() {
        return tableExists("wiki_articles") && tableExists("wiki_activity");
    }

    std::vector<json::object> allRowsLocked(const std::string& table) {
        QueryBuilder builder = makeBuilder("GET", table);
        const auto response = builder.exec();
        if (response.status == http::status::internal_server_error) {
            throw std::runtime_error(response.message);
        }

        std::vector<json::object> rows;
        for (const auto& item : response.data) {
            if (item.is_object()) {
                rows.push_back(item.as_object());
            }
        }
        return rows;
    }

    std::optional<json::object> rowByIdLocked(const std::string& table, int id) {
        QueryBuilder builder = makeBuilder("GET", table);
        builder.addFilter("id=" + std::to_string(id));
        const auto response = builder.exec();
        if (response.status == http::status::internal_server_error) {
            throw std::runtime_error(response.message);
        }
        if (response.data.empty() || !response.data[0].is_object()) {
            return std::nullopt;
        }
        return response.data[0].as_object();
    }

    std::vector<WikiArticle> allArticlesLocked() {
        std::vector<WikiArticle> articles;
        for (const auto& row : allRowsLocked("wiki_articles")) {
            articles.push_back(rowToArticle(row));
        }
        return articles;
    }

    std::optional<WikiArticle> getArticleLocked(int id) {
        const auto row = rowByIdLocked("wiki_articles", id);
        if (!row) {
            return std::nullopt;
        }
        return rowToArticle(*row);
    }

    WikiArticle rowToArticle(const json::object& row) {
        WikiArticle article;
        article.id = objectInt(row, "id");
        article.title = objectString(row, "title");
        article.slug = objectString(row, "slug");
        article.summary = objectString(row, "summary");
        article.content = objectString(row, "content");
        article.category = objectString(row, "category");
        article.status = objectString(row, "status");
        article.author = objectString(row, "author");
        article.tags = objectString(row, "tags");
        article.templateName = objectString(row, "template");
        article.readingTime = objectInt(row, "reading_time", 4);
        article.favorite = objectBool(row, "favorite");
        article.views = objectInt(row, "views");
        article.createdAt = objectString(row, "created_at");
        article.updatedAt = objectString(row, "updated_at");
        return article;
    }

    static void sortArticles(std::vector<WikiArticle>& articles) {
        std::sort(articles.begin(), articles.end(), [](const WikiArticle& lhs, const WikiArticle& rhs) {
            if (lhs.updatedAt != rhs.updatedAt) {
                return lhs.updatedAt > rhs.updatedAt;
            }
            return lhs.id > rhs.id;
        });
    }

    json::object articleData(const ArticleInput& input) {
        json::object data;
        data["title"] = input.title;
        data["summary"] = input.summary;
        data["content"] = input.content;
        data["category"] = input.category;
        data["status"] = input.status;
        data["author"] = input.author;
        data["tags"] = input.tags;
        data["template"] = input.templateName;
        data["reading_time"] = input.readingTime;
        return data;
    }

    WikiArticle insertArticleLocked(const ArticleInput& input,
                                    const std::string& createdAt,
                                    const std::string& updatedAt,
                                    bool favorite,
                                    int views) {
        json::object data = articleData(input);
        data["slug"] = makeSlug(input.title);
        data["favorite"] = favorite ? 1 : 0;
        data["views"] = views;
        data["created_at"] = createdAt;
        data["updated_at"] = updatedAt;

        QueryBuilder builder = makeBuilder("POST", "wiki_articles");
        builder.setData(data);
        const auto response = builder.exec();
        if (response.status != http::status::created && response.status != http::status::ok) {
            throw std::runtime_error(response.message);
        }
        if (response.data.empty() || !response.data[0].is_object()) {
            throw std::runtime_error("ORM did not return created article id");
        }

        const int id = objectInt(response.data[0].as_object(), "id");
        const auto article = getArticleLocked(id);
        if (!article) {
            throw std::runtime_error("Created article was not found");
        }
        return *article;
    }

    void updateByIdLocked(const std::string& table, int id, const json::object& data) {
        QueryBuilder builder = makeBuilder("PUT", table);
        builder.setData(data);
        builder.addFilter("id=" + std::to_string(id));
        const auto response = builder.exec();
        if (response.status != http::status::ok) {
            throw std::runtime_error(response.message);
        }
    }

    void addActivityLocked(int articleId,
                           const std::string& actor,
                           const std::string& action,
                           const std::string& detail,
                           const std::string& createdAt = nowTimestamp()) {
        json::object data;
        data["article_id"] = articleId;
        data["actor"] = actor.empty() ? "Wiki Team" : actor;
        data["action"] = action;
        data["detail"] = detail;
        data["created_at"] = createdAt;

        QueryBuilder builder = makeBuilder("POST", "wiki_activity");
        builder.setData(data);
        const auto response = builder.exec();
        if (response.status != http::status::created && response.status != http::status::ok) {
            throw std::runtime_error(response.message);
        }
    }

    void seedIfEmpty() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!allArticlesLocked().empty()) {
            return;
        }

        const auto onboarding = insertArticleLocked({
            "Onboarding нового сотрудника",
            "Пошаговая инструкция по доступам, рабочим процессам, встречам и базовым правилам команды.",
            "Эта статья помогает быстро провести нового сотрудника через первые шаги в компании.\n\n## Первые действия\n- Выдать доступ к рабочим инструментам и корпоративным сервисам.\n- Добавить сотрудника в командные чаты и внутренние пространства wiki.\n- Назначить приветственную встречу и ответственного buddy.\n- Передать список обязательных материалов для ознакомления.\n\n## Контрольная точка\nЧерез неделю проверьте, что сотрудник понимает свою зону ответственности, знает основные процессы и получил все необходимые доступы.",
            "People Ops", "published", "Анна Петрова", "HR, Onboarding, Process", "Guide", 6
        }, "2026-04-22 10:30:00", "2026-04-27 15:20:00", true, 42);

        const auto design = insertArticleLocked({
            "Дизайн-система и UI компоненты",
            "Библиотека компонентов, правила использования токенов, сеток, отступов и типографики.",
            "Статья фиксирует договоренности по интерфейсам и помогает поддерживать единый визуальный язык продукта.\n\n## Состав\n- Цветовые токены и состояния компонентов.\n- Правила типографики и плотности интерфейса.\n- Паттерны форм, таблиц, фильтров и модальных окон.\n\n## Обновление\nПеред изменением компонента добавьте пример состояния, проверьте адаптивность и опишите ограничения использования.",
            "Design", "published", "Мария Соколова", "Design, UI Kit, Components", "Reference", 8
        }, "2026-04-18 11:10:00", "2026-04-30 09:45:00", true, 35);

        const auto release = insertArticleLocked({
            "Релизный процесс продукта",
            "Чек-лист по подготовке, согласованию и публикации релизов для продуктовой команды.",
            "Документ описывает порядок подготовки релиза от freeze-окна до публикации заметок.\n\n## Перед релизом\n- Проверить список задач и миграций.\n- Подтвердить владельцев проверки.\n- Обновить changelog и материалы поддержки.\n\n## После релиза\nСоберите метрики, ошибки и обратную связь, затем зафиксируйте выводы в этой статье.",
            "Product", "draft", "Иван Орлов", "Release, Product, Checklist", "Checklist", 5
        }, "2026-04-25 13:00:00", "2026-04-29 17:15:00", false, 18);

        insertArticleLocked({
            "API соглашения backend-команды",
            "Единые правила именования эндпоинтов, формата ошибок, пагинации и версионирования API.",
            "Соглашения нужны, чтобы клиенты получали предсказуемые ответы, а команды могли безопасно развивать API.\n\n## Ответы\n- Успешные ответы возвращают объект или массив данных.\n- Ошибки содержат error, status и понятное сообщение.\n- Пагинация использует limit и offset.\n\n## Версионирование\nBreaking changes публикуются только в новой версии API.",
            "Engineering", "published", "Дмитрий Ким", "API, Backend, Standards", "Reference", 7
        }, "2026-04-12 08:20:00", "2026-05-01 12:05:00", false, 51);

        insertArticleLocked({
            "Планирование маркетинговых кампаний",
            "Шаблон подготовки кампании: аудитория, сообщение, каналы, метрики и ответственные.",
            "Статья помогает запускать кампании по единой структуре и не терять договоренности между командами.\n\n## Структура\n- Цель кампании и целевая аудитория.\n- Ключевое сообщение и ограничения.\n- Каналы, бюджет и календарь публикаций.\n- Метрики успеха и владелец отчета.",
            "Marketing", "published", "Ольга Никифорова", "Marketing, Campaigns, Planning", "Template", 4
        }, "2026-04-16 14:40:00", "2026-04-26 10:00:00", false, 23);

        addActivityLocked(onboarding.id, "Мария", "обновила раздел", "Доступы в статье onboarding", "2026-05-02 08:30:00");
        addActivityLocked(release.id, "Иван", "добавил чек-лист", "Подготовка релиза", "2026-05-01 15:10:00");
        addActivityLocked(design.id, "Ольга", "создала категорию", "Design", "2026-04-30 12:40:00");
    }
};

inline std::string queryParam(const urls::url_view& url, const std::string& key) {
    for (const auto& param : url.params()) {
        if (std::string(param.key) == key) {
            return std::string(param.value);
        }
    }
    return "";
}

inline int parseId(const std::map<std::string, std::string>& params) {
    const auto it = params.find("id");
    if (it == params.end()) {
        throw std::invalid_argument("Article id is required");
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        throw std::invalid_argument("Article id must be numeric");
    }
}

inline void sendJson(http::response<http::string_body>& res,
                     http::status status,
                     const json::value& body) {
    res.result(status);
    res.set(http::field::content_type, "application/json; charset=utf-8");
    res.set(http::field::cache_control, "no-store");
    res.body() = json::serialize(body);
    res.prepare_payload();
}

inline void sendError(http::response<http::string_body>& res,
                      http::status status,
                      const std::string& message) {
    json::object body;
    body["error"] = message;
    body["status"] = static_cast<int>(status);
    sendJson(res, status, body);
}

class WikiPageHandler : public HandlerBase {
protected:
    void handleGet(const http::request<http::string_body>&,
                   http::response<http::string_body>& res,
                   const urls::url_view&,
                   const std::map<std::string, std::string>&) override {
        std::ifstream file(templatePath());
        if (!file.is_open()) {
            buildTextResponse(res, http::status::internal_server_error, "Cannot open wiki template");
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        buildHtmlResponse(res, http::status::ok, buffer.str());
    }
};

class WikiDetailPageHandler : public HandlerBase {
public:
    explicit WikiDetailPageHandler(std::shared_ptr<WikiRepository> repository)
        : repository_(std::move(repository)) {}

protected:
    void handleGet(const http::request<http::string_body>&,
                   http::response<http::string_body>& res,
                   const urls::url_view&,
                   const std::map<std::string, std::string>& params) override {
        try {
            const auto articleId = parseId(params);
            auto article = repository_->getArticle(articleId, false);
            if (!article) {
                buildTextResponse(res, http::status::not_found, "Article not found");
                return;
            }

            // Read template
            std::ifstream file(detailTemplatePath());
            if (!file.is_open()) {
                buildTextResponse(res, http::status::internal_server_error, "Cannot open wiki detail template");
                return;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string html = buffer.str();

            // Escape HTML values
            auto esc = [](const std::string& s) -> std::string {
                std::string result;
                for (char c : s) {
                    switch (c) {
                        case '&':  result += "&amp;"; break;
                        case '<':  result += "&lt;"; break;
                        case '>':  result += "&gt;"; break;
                        case '"':  result += "&quot;"; break;
                        case '\'': result += "&#039;"; break;
                        default:   result += c; break;
                    }
                }
                return result;
            };

            // Build tags HTML
            std::string tagsHtml;
            {
                std::stringstream tagStream(article->tags);
                std::string tag;
                while (std::getline(tagStream, tag, ',')) {
                    tag.erase(0, tag.find_first_not_of(" \t"));
                    tag.erase(tag.find_last_not_of(" \t") + 1);
                    if (!tag.empty()) {
                        tagsHtml += "<span class=\"tag\">" + esc(tag) + "</span>";
                    }
                }
            }

            // Build content HTML (simple markdown-like)
            std::string contentHtml;
            {
                std::istringstream stream(article->content);
                std::string line;
                std::vector<std::string> listItems;
                auto flushList = [&]() {
                    if (!listItems.empty()) {
                        contentHtml += "<ul>" +
                            [&]() -> std::string {
                                std::string items;
                                for (const auto& item : listItems) {
                                    items += "<li>" + esc(item) + "</li>";
                                }
                                return items;
                            }() + "</ul>";
                        listItems.clear();
                    }
                };

                while (std::getline(stream, line)) {
                    // Trim
                    size_t start = line.find_first_not_of(" \t");
                    if (start != std::string::npos) line = line.substr(start);
                    size_t end = line.find_last_not_of(" \t");
                    if (end != std::string::npos) line = line.substr(0, end + 1);

                    if (line.empty()) {
                        flushList();
                        continue;
                    }
                    if (line.substr(0, 2) == "- ") {
                        listItems.push_back(line.substr(2));
                        continue;
                    }
                    flushList();
                    if (line.substr(0, 3) == "## ") {
                        contentHtml += "<h3>" + esc(line.substr(3)) + "</h3>";
                    } else {
                        contentHtml += "<p>" + esc(line) + "</p>";
                    }
                }
                flushList();
            }

            // Status display
            std::string statusDisplay = article->status == "published" ? "Актуально" : "Черновик";
            std::string statusClass = article->status == "published" ? "status-published" : "status-draft";

            // Replace placeholders
            std::vector<std::pair<std::string, std::string>> replacements = {
                {"{{TITLE}}", esc(article->title)},
                {"{{SUMMARY}}", esc(article->summary)},
                {"{{CONTENT}}", contentHtml},
                {"{{AUTHOR}}", esc(article->author)},
                {"{{CATEGORY}}", esc(article->category)},
                {"{{STATUS}}", statusDisplay},
                {"{{STATUS_CLASS}}", statusClass},
                {"{{TEMPLATE}}", esc(article->templateName)},
                {"{{TAGS}}", tagsHtml},
                {"{{READING_TIME}}", std::to_string(article->readingTime)},
                {"{{VIEWS}}", std::to_string(article->views)},
                {"{{CREATED_AT}}", esc(article->createdAt)},
                {"{{UPDATED_AT}}", esc(article->updatedAt)},
            };

            for (const auto& [placeholder, value] : replacements) {
                size_t pos = 0;
                while ((pos = html.find(placeholder, pos)) != std::string::npos) {
                    html.replace(pos, placeholder.length(), value);
                    pos += value.length();
                }
            }

            buildHtmlResponse(res, http::status::ok, html);
        } catch (const std::exception& e) {
            buildTextResponse(res, http::status::internal_server_error, e.what());
        }
    }

private:
    std::shared_ptr<WikiRepository> repository_;
};

class WikiHealthHandler : public HandlerBase {
public:
    explicit WikiHealthHandler(std::shared_ptr<WikiRepository> repository)
        : repository_(std::move(repository)) {}

protected:
    void handleGet(const http::request<http::string_body>&,
                   http::response<http::string_body>& res,
                   const urls::url_view&,
                   const std::map<std::string, std::string>&) override {
        json::object body;
        body["status"] = "ok";
        body["database"] = repository_->path();
        body["articles"] = repository_->stats()["articles"];
        sendJson(res, http::status::ok, body);
    }

private:
    std::shared_ptr<WikiRepository> repository_;
};

class WikiApiHandler : public HandlerBase {
public:
    explicit WikiApiHandler(std::shared_ptr<WikiRepository> repository)
        : repository_(std::move(repository)) {}

protected:
    void handleGet(const http::request<http::string_body>&,
                   http::response<http::string_body>& res,
                   const urls::url_view& url,
                   const std::map<std::string, std::string>& params) override {
        try {
            const std::string path(url.path());
            if (path == "/api/wiki/stats") {
                sendJson(res, http::status::ok, repository_->stats());
                return;
            }

            if (path == "/api/wiki/categories") {
                json::object body;
                body["categories"] = repository_->categories();
                sendJson(res, http::status::ok, body);
                return;
            }

            if (path == "/api/wiki/activity") {
                json::object body;
                body["activity"] = repository_->activity(12);
                sendJson(res, http::status::ok, body);
                return;
            }

            if (path == "/api/wiki/articles") {
                WikiFilters filters;
                filters.query = queryParam(url, "q");
                filters.category = queryParam(url, "category");
                filters.status = queryParam(url, "status");
                filters.favoritesOnly = queryParam(url, "favorite") == "1";
                filters.recentOnly = queryParam(url, "recent") == "1";
                const auto limit = queryParam(url, "limit");
                if (!limit.empty()) {
                    try {
                        filters.limit = std::max(1, std::min(100, std::stoi(limit)));
                    } catch (...) {
                        filters.limit = 0;
                    }
                }

                json::array articles;
                for (const auto& article : repository_->listArticles(filters)) {
                    articles.emplace_back(articleToJson(article));
                }

                json::object body;
                body["articles"] = articles;
                sendJson(res, http::status::ok, body);
                return;
            }

            if (path.find("/api/wiki/articles/") == 0 && path.rfind("/favorite") == std::string::npos) {
                const auto article = repository_->getArticle(parseId(params));
                if (!article) {
                    sendError(res, http::status::not_found, "Article not found");
                    return;
                }
                sendJson(res, http::status::ok, articleToJson(*article));
                return;
            }

            sendError(res, http::status::not_found, "API route not found");
        } catch (const std::invalid_argument& e) {
            sendError(res, http::status::bad_request, e.what());
        } catch (const std::exception& e) {
            sendError(res, http::status::internal_server_error, e.what());
        }
    }

    void handlePost(const http::request<http::string_body>& req,
                    http::response<http::string_body>& res,
                    const urls::url_view& url,
                    const std::map<std::string, std::string>&) override {
        try {
            if (std::string(url.path()) != "/api/wiki/articles") {
                sendError(res, http::status::not_found, "API route not found");
                return;
            }
            const auto input = parseArticleInput(req.body());
            const auto article = repository_->createArticle(input);
            sendJson(res, http::status::created, articleToJson(article));
        } catch (const std::invalid_argument& e) {
            sendError(res, http::status::bad_request, e.what());
        } catch (const std::exception& e) {
            sendError(res, http::status::internal_server_error, e.what());
        }
    }

    void handlePut(const http::request<http::string_body>& req,
                   http::response<http::string_body>& res,
                   const urls::url_view& url,
                   const std::map<std::string, std::string>& params) override {
        try {
            const std::string path(url.path());
            if (path.find("/api/wiki/articles/") != 0 || path.rfind("/favorite") != std::string::npos) {
                sendError(res, http::status::not_found, "API route not found");
                return;
            }

            const auto input = parseArticleInput(req.body());
            const auto article = repository_->updateArticle(parseId(params), input);
            if (!article) {
                sendError(res, http::status::not_found, "Article not found");
                return;
            }
            sendJson(res, http::status::ok, articleToJson(*article));
        } catch (const std::invalid_argument& e) {
            sendError(res, http::status::bad_request, e.what());
        } catch (const std::exception& e) {
            sendError(res, http::status::internal_server_error, e.what());
        }
    }

    void handlePatch(const http::request<http::string_body>&,
                     http::response<http::string_body>& res,
                     const urls::url_view& url,
                     const std::map<std::string, std::string>& params) override {
        try {
            if (std::string(url.path()).rfind("/favorite") == std::string::npos) {
                sendError(res, http::status::not_found, "API route not found");
                return;
            }
            const auto article = repository_->toggleFavorite(parseId(params));
            if (!article) {
                sendError(res, http::status::not_found, "Article not found");
                return;
            }
            sendJson(res, http::status::ok, articleToJson(*article));
        } catch (const std::invalid_argument& e) {
            sendError(res, http::status::bad_request, e.what());
        } catch (const std::exception& e) {
            sendError(res, http::status::internal_server_error, e.what());
        }
    }

    void handleDelete(const http::request<http::string_body>&,
                      http::response<http::string_body>& res,
                      const urls::url_view& url,
                      const std::map<std::string, std::string>& params) override {
        try {
            const std::string path(url.path());
            if (path.find("/api/wiki/articles/") != 0 || path.rfind("/favorite") != std::string::npos) {
                sendError(res, http::status::not_found, "API route not found");
                return;
            }

            if (!repository_->deleteArticle(parseId(params))) {
                sendError(res, http::status::not_found, "Article not found");
                return;
            }

            json::object body;
            body["success"] = true;
            sendJson(res, http::status::ok, body);
        } catch (const std::invalid_argument& e) {
            sendError(res, http::status::bad_request, e.what());
        } catch (const std::exception& e) {
            sendError(res, http::status::internal_server_error, e.what());
        }
    }

private:
    std::shared_ptr<WikiRepository> repository_;
};

} // namespace wiki_example
