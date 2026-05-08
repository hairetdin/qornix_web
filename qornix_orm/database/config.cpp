/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <functional>

#ifdef QORNIX_USE_YAML
#include <yaml-cpp/yaml.h>
#endif

#ifdef QORNIX_USE_YAML
namespace {
void setConfigValueWithDatabaseAliases(
    const std::function<void(const std::string&, const std::string&)>& setter,
    const std::string& key,
    const std::string& value
) {
    setter(key, value);

    constexpr const char* dbPrefix = "db.";
    constexpr const char* databasePrefix = "database.";

    if (key.rfind(dbPrefix, 0) == 0) {
        setter(std::string(databasePrefix) + key.substr(3), value);
    } else if (key.rfind(databasePrefix, 0) == 0) {
        setter(std::string(dbPrefix) + key.substr(9), value);
    }
}
} // namespace
#endif

Config &Config::getInstance() {
    static Config instance;
    return instance;
}

void Config::loadFromFile(const std::string &filepath) {
    // Auto-detect format by extension
    const std::string ext = filepath.substr(filepath.find_last_of('.'));

    if (ext == ".yaml" || ext == ".yml") {
        loadFromYamlFile(filepath);
        return;
    }

    std::ifstream file(filepath);

    // If file not found, try load from standard locations
    if (!file.is_open()) {
        // Try loading from the project home directory
        std::string altPath1 = "../" + filepath;
        file.open(altPath1);
    }

    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filepath);
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty rows and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Split by sign =
        size_t delimiterPos = line.find('=');
        if (delimiterPos != std::string::npos) {
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);

            // Remove spaces
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            set(key, value);
        }
    }

    // Fill configuration database from settings
    db_config_.host = get("db.host");

    // Port only for PostgreSQL/MySQL
    std::string portStr = get("db.port");
    if (!portStr.empty()) {
        db_config_.port = std::stoi(portStr);
    }

    // Name DB or path to file for SQLite
    std::string dbName = get("db.name");
    if (!dbName.empty()) {
        db_config_.dbname = dbName;
    }

    db_config_.user = get("db.user");
    db_config_.password = get("db.password");
    db_config_.driver = get("db.driver");

    // Connection string (priority over other parameters)
    std::string connectionString = get("db.connection_string");
    if (!connectionString.empty()) {
        db_config_.connectionString = connectionString;

        // For SQLite Use connection_string as dbname if dbname not specified
        if (db_config_.driver == "sqlite" && db_config_.dbname.empty()) {
            db_config_.dbname = connectionString;
        }
    } else {
        // Create connection string from parameters if not specified explicitly
        db_config_.connectionString = createConnectionString(db_config_);
    }
}

#ifdef QORNIX_USE_YAML
void Config::loadFromYamlFile(const std::string &filepath) {
    YAML::Node node;
    try {
        node = YAML::LoadFile(filepath);
    } catch (const YAML::Exception &e) {
        throw std::runtime_error("YAML parse error in file: " + filepath + " — " + e.what());
    }

    // Flatten nested YAML into dot-separated flat keys.
    // Keep legacy `db.*` and framework-level `database.*` aliases in sync.
    std::function<void(const YAML::Node &, const std::string &)> flatten;
    flatten = [&](const YAML::Node &parent, const std::string &prefix) {
        for (const auto &it : parent) {
            std::string key = it.first.as<std::string>();
            const auto &val = it.second;
            std::string full_key = prefix.empty() ? key : prefix + "." + key;

            if (val.IsMap()) {
                flatten(val, full_key);
            } else if (val.IsSequence()) {
                // Skip sequences
            } else if (val.IsScalar() || val.IsDefined()) {
                setConfigValueWithDatabaseAliases(
                    [this](const std::string& k, const std::string& v) { set(k, v); },
                    full_key,
                    val.as<std::string>()
                );
            }
        }
    };

    flatten(node, "");

    auto getDbValue = [this](const std::string& name) {
        const std::string dbKey = "db." + name;
        const std::string databaseKey = "database." + name;
        const std::string value = get(dbKey);
        return !value.empty() ? value : get(databaseKey);
    };

    // Populate db_config_ from settings (same logic as loadFromFile)
    db_config_.host = getDbValue("host");

    std::string portStr = getDbValue("port");
    if (!portStr.empty()) {
        db_config_.port = std::stoi(portStr);
    }

    std::string dbName = getDbValue("name");
    std::string sqlitePath = getDbValue("sqlite_path");
    if (!dbName.empty()) {
        db_config_.dbname = dbName;
    } else if (!sqlitePath.empty()) {
        db_config_.dbname = sqlitePath;
    }

    db_config_.user = getDbValue("user");
    db_config_.password = getDbValue("password");
    db_config_.driver = getDbValue("driver");

    std::string connectionString = getDbValue("connection_string");
    if (!connectionString.empty()) {
        db_config_.connectionString = connectionString;
        if ((db_config_.driver == "sqlite" || db_config_.driver == "sqlite3") && db_config_.dbname.empty()) {
            db_config_.dbname = connectionString;
        }
    } else {
        db_config_.connectionString = createConnectionString(db_config_);
    }
}
#else
void Config::loadFromYamlFile(const std::string &filepath) {
    throw std::runtime_error("YAML config support not compiled — rebuild with yaml-cpp");
}
#endif


void Config::loadFromEnvironment() {
    // Load from variables environment
    const char *db_host = std::getenv("DB_HOST");
    if (db_host) db_config_.host = db_host;

    const char *db_port = std::getenv("DB_PORT");
    if (db_port) db_config_.port = std::stoi(db_port);

    const char *db_name = std::getenv("DB_NAME");
    if (db_name) db_config_.dbname = db_name;

    const char *db_user = std::getenv("DB_USER");
    if (db_user) db_config_.user = db_user;

    const char *db_password = std::getenv("DB_PASSWORD");
    if (db_password) db_config_.password = db_password;

    const char *db_driver = std::getenv("DB_DRIVER");
    if (db_driver) db_config_.driver = db_driver;
}

DatabaseConfig Config::getDatabaseConfig() const {
    return db_config_;
}

void Config::setDatabaseConfig(const DatabaseConfig &config) {
    db_config_ = config;
}

std::string Config::get(const std::string &key) const {
    auto it = settings_.find(key);
    if (it != settings_.end()) {
        return it->second;
    }
    return "";
}

void Config::set(const std::string &key, const std::string &value) {
    settings_[key] = value;
}

// Function for creation connection string from configuration
// Converts structure DatabaseConfig in connection string in format PostgreSQL
// Parameters:
//   config - Structure with settings connection to database data
// Returns:
//   Connection string in format "host=... port=... dbname=... user=... password=... "
//   Contains only non-empty parameters, each parameter separated space
std::string createConnectionString(const DatabaseConfig &config) {
    if (config.driver == "sqlite" || config.driver == "sqlite3") {
        if (!config.connectionString.empty()) {
            return config.connectionString;
        }
        return config.dbname;
    }

    std::ostringstream oss;
    if (!config.host.empty()) {
        oss << "host=" << config.host << " ";
    }
    if (config.port > 0) {
        oss << "port=" << config.port << " ";
    }
    if (!config.dbname.empty()) {
        oss << "dbname=" << config.dbname << " ";
    }
    if (!config.user.empty()) {
        oss << "user=" << config.user << " ";
    }
    if (!config.password.empty()) {
        oss << "password=" << config.password << " ";
    }
    return oss.str();
}

// Method for extraction parameters from connection string
void Config::loadFromConnectionString(const std::string &connectionString, const std::string &driverType) {
    // DatabaseConfig config;
    db_config_ = DatabaseConfig{};
    db_config_.driver = driverType;

    if (driverType == "sqlite" || driverType == "sqlite3") {
        db_config_.dbname = connectionString;
        db_config_.connectionString = connectionString;
        return;
    }

    // Parse connection string, separated spaces
    std::istringstream iss(connectionString);
    std::string token;

    while (iss >> token) {
        // Check format key=value
        size_t pos = token.find('=');
        if (pos != std::string::npos) {
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);

            // Remove possible spaces and quotes
            if (!value.empty() && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }

            // Map keys with fields configuration
            if (key == "host") {
                db_config_.host = value;
            } else if (key == "port") {
                db_config_.port = std::stoi(value);
            } else if (key == "dbname") {
                db_config_.dbname = value;
            } else if (key == "user") {
                db_config_.user = value;
            } else if (key == "password") {
                db_config_.password = value;
            } else if (key == "driver") {
                db_config_.driver = value;
            }
        }
    }

    // Save original connection string
    db_config_.connectionString = createConnectionString(db_config_);
}

void Config::reset() {
    db_config_ = DatabaseConfig{};
    settings_.clear();
}
