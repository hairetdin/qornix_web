#pragma once

#include "app_paths.h"
#include "config.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <string>

namespace dynamic_api_app_runtime {

struct RuntimeConfig {
    DatabaseConfig databaseConfig{};
    std::filesystem::path uploadedSchemaPath;
    std::filesystem::path exportedSchemaPath;
    std::filesystem::path historyPath;
    std::filesystem::path schemaXsdPath;
    std::filesystem::path configPath;
    std::string apiPrefix = "/api/dynamic";
    std::string schemaPrefix = "/api/dynamic/schema";
    std::size_t defaultLimit = 100;
    std::size_t maxLimit = 1000;
    bool allowRawFilter = false;
    bool allowRawJoin = true;
    bool allowUpdateWithoutFilter = false;
    bool allowDeleteWithoutFilter = false;
    bool allowDestructiveSchemaApply = false;
};

inline bool fileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

inline std::filesystem::path resolvePath(
    const std::string& configuredPath,
    const std::filesystem::path& fallbackPath
) {
    if (configuredPath.empty()) {
        return fallbackPath;
    }

    std::filesystem::path path(configuredPath);
    if (path.is_absolute()) {
        return path;
    }

    return dynamic_api_app_paths::sourceDir() / path;
}

inline std::string firstValue(Config& config, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const std::string value = config.get(key);
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

inline bool boolValue(Config& config, std::initializer_list<const char*> keys, bool fallback) {
    std::string value = firstValue(config, keys);
    if (value.empty()) {
        return fallback;
    }
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

inline std::size_t sizeValue(Config& config, std::initializer_list<const char*> keys, std::size_t fallback) {
    const std::string value = firstValue(config, keys);
    if (value.empty()) {
        return fallback;
    }
    try {
        return static_cast<std::size_t>(std::stoull(value));
    } catch (...) {
        return fallback;
    }
}

inline void applyDatabaseConfig(RuntimeConfig& runtime, Config& config) {
    DatabaseConfig loaded = config.getDatabaseConfig();
    if (!loaded.driver.empty()) {
        runtime.databaseConfig.driver = loaded.driver;
    }
    if (!loaded.dbname.empty()) {
        runtime.databaseConfig.dbname = loaded.dbname;
    }
    if (!loaded.connectionString.empty()) {
        runtime.databaseConfig.connectionString = loaded.connectionString;
    }

    const std::string configuredDriver = firstValue(config, {
        "database.driver",
        "database.type",
        "db.driver",
        "db.type"
    });
    if (!configuredDriver.empty()) {
        runtime.databaseConfig.driver = configuredDriver == "sqlite3" ? "sqlite" : configuredDriver;
    }
    if (runtime.databaseConfig.driver.empty()) {
        runtime.databaseConfig.driver = "sqlite";
    }

    if (runtime.databaseConfig.driver == "sqlite" || runtime.databaseConfig.driver == "sqlite3") {
        std::string dbPath = firstValue(config, {
            "database.path",
            "database.database",
            "database.sqlite_path",
            "database.name",
            "database.connection_string",
            "db.path",
            "db.database",
            "db.sqlite_path",
            "db.name",
            "db.connection_string"
        });
        if (dbPath.empty()) {
            dbPath = runtime.databaseConfig.dbname;
        }

        runtime.databaseConfig.driver = "sqlite";
        runtime.databaseConfig.dbname = resolvePath(dbPath, dynamic_api_app_paths::databasePath()).string();
        runtime.databaseConfig.connectionString = runtime.databaseConfig.dbname;
        return;
    }

    runtime.databaseConfig.host = firstValue(config, {"database.host", "db.host"});
    const std::string port = firstValue(config, {"database.port", "db.port"});
    if (!port.empty()) {
        try {
            runtime.databaseConfig.port = std::stoi(port);
        } catch (...) {
        }
    }
    runtime.databaseConfig.dbname = firstValue(config, {"database.name", "database.database", "db.name", "db.database"});
    runtime.databaseConfig.user = firstValue(config, {"database.user", "db.user"});
    runtime.databaseConfig.password = firstValue(config, {"database.password", "db.password"});
    runtime.databaseConfig.connectionString = firstValue(config, {
        "database.connection_string",
        "db.connection_string"
    });
}

inline RuntimeConfig loadRuntimeConfig() {
    RuntimeConfig runtime;
    runtime.databaseConfig.driver = "sqlite";
    runtime.databaseConfig.dbname = dynamic_api_app_paths::databasePath().string();
    runtime.databaseConfig.connectionString = runtime.databaseConfig.dbname;
    runtime.uploadedSchemaPath = dynamic_api_app_paths::uploadedSchemaPath();
    runtime.exportedSchemaPath = dynamic_api_app_paths::exportedSchemaPath();
    runtime.historyPath = dynamic_api_app_paths::historyPath();
    runtime.schemaXsdPath = dynamic_api_app_paths::schemaAppXsdPath();
    runtime.configPath = dynamic_api_app_paths::sourceDir() / "config.yaml";

    if (!fileExists(runtime.configPath)) {
        return runtime;
    }

    Config& config = Config::getInstance();
    config.reset();
    try {
        config.loadFromFile(runtime.configPath.string());
    } catch (...) {
        return runtime;
    }

    applyDatabaseConfig(runtime, config);

    runtime.uploadedSchemaPath = resolvePath(
        firstValue(config, {"dynamic_api.schema.file", "dynamic_api.schema.uploaded_file"}),
        runtime.uploadedSchemaPath
    );
    runtime.exportedSchemaPath = resolvePath(
        firstValue(config, {"dynamic_api.schema.exported_file", "dynamic_api.schema.database_file"}),
        runtime.exportedSchemaPath
    );
    runtime.historyPath = resolvePath(
        firstValue(config, {"dynamic_api.schema.history_file"}),
        runtime.historyPath
    );
    runtime.schemaXsdPath = resolvePath(
        firstValue(config, {"dynamic_api.schema.xsd_file"}),
        runtime.schemaXsdPath
    );

    const std::string apiPrefix = firstValue(config, {"dynamic_api.route_prefix"});
    if (!apiPrefix.empty()) {
        runtime.apiPrefix = apiPrefix;
    }
    const std::string schemaPrefix = firstValue(config, {"dynamic_api.schema_prefix"});
    if (!schemaPrefix.empty()) {
        runtime.schemaPrefix = schemaPrefix;
    }

    runtime.defaultLimit = sizeValue(config, {"dynamic_api.query.default_limit"}, runtime.defaultLimit);
    runtime.maxLimit = sizeValue(config, {"dynamic_api.query.max_limit"}, runtime.maxLimit);
    runtime.allowRawFilter = boolValue(config, {"dynamic_api.query.allow_raw_filter"}, runtime.allowRawFilter);
    runtime.allowRawJoin = boolValue(config, {"dynamic_api.query.allow_raw_join"}, runtime.allowRawJoin);
    runtime.allowUpdateWithoutFilter = boolValue(
        config,
        {"dynamic_api.query.allow_update_without_filter"},
        runtime.allowUpdateWithoutFilter
    );
    runtime.allowDeleteWithoutFilter = boolValue(
        config,
        {"dynamic_api.query.allow_delete_without_filter"},
        runtime.allowDeleteWithoutFilter
    );
    runtime.allowDestructiveSchemaApply = boolValue(
        config,
        {"dynamic_api.schema.allow_destructive_apply"},
        runtime.allowDestructiveSchemaApply
    );

    return runtime;
}

inline const RuntimeConfig& config() {
    static const RuntimeConfig runtime = loadRuntimeConfig();
    return runtime;
}

} // namespace dynamic_api_app_runtime
