/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "config_interface.h"
#include <fstream>
#include <functional>
#include <vector>
#include <string>
#include <filesystem>
#include <unistd.h>

#ifdef QORNIX_USE_YAML
#include <yaml-cpp/yaml.h>
#endif

static std::string resolveExeDir() {
    // Try to find the directory containing the executable
    // via /proc/self/exe (Linux) or argv[0] (passed from caller)
    std::string exePath;
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        exePath = std::string(buf);
    }
    if (!exePath.empty()) {
        auto dir = std::filesystem::path(exePath).parent_path().string();
        if (!dir.empty() && dir != ".") {
            return dir;
        }
    }
    return "";
}

ConfigInterface::ConfigInterface() {
    // if (!loadFromFile("config.ini")) {
    //     std::cout << "not succeeded load configuration from config.ini" << std::endl;
    // }
}

void ConfigInterface::set(const std::string& key, const std::string& value) {
    configValues_[key] = value;
}

std::string ConfigInterface::get(const std::string& key, const std::string& defaultValue) const {
    auto it = configValues_.find(key);
    return (it != configValues_.end()) ? it->second : defaultValue;
}

bool ConfigInterface::loadFromFile(const std::string& configFilePath) {
    // Auto-detect format by extension
    const std::string lowerPath = configFilePath;
    const std::string ext = lowerPath.substr(lowerPath.find_last_of('.'));

    if (ext == ".yaml" || ext == ".yml") {
        // Resolve exeDir once for fallback
        static const std::string cachedExeDir = resolveExeDir();
        return loadFromYamlFile(configFilePath, cachedExeDir);
    }

    // Legacy INI / flat key=value format
    std::ifstream file(configFilePath);
    if (!file.is_open()) {
        // Try loading from the project home directory
        std::string altPath1 = "../" + configFilePath;
        file.open(altPath1);
    }

    if (!file.is_open()) {
        std::cerr << "Failed open file: " << configFilePath << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        // std::cout << "DEBUG: Config file, read line: " << line << std::endl;
        // Look for '=' to separate key and value
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            // Trim leading and trailing whitespace
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            // Remove quotes from the value if present
            if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }

            configValues_[key] = value;
        }
    }

    file.close();
    return true;
}

bool ConfigInterface::saveToFile(const std::string& configFile) const {
    std::ofstream file(configFile);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& pair : configValues_) {
        file << pair.first << "=" << pair.second << std::endl;
    }

    file.close();
    return true;
}

void ConfigInterface::printConfig() const {
    std::cout << "=== Configuration ===" << std::endl;
    for (const auto& pair : configValues_) {
        std::cout << pair.first << "=" << pair.second << std::endl;
    }
    std::cout << "=====================" << std::endl;
}

#ifdef QORNIX_USE_YAML
bool ConfigInterface::loadFromYamlFile(const std::string& yamlFilePath, const std::string& exeDir) {
    // Try multiple locations for the YAML file:
    // 1. The path as-is (absolute or relative to cwd)
    // 2. Parent directory (../config.yaml) — works when binary is in build subdirectory
    // 3. Same directory as executable (exeDir/config.yaml) — works when run from elsewhere
    std::vector<std::string> candidates = {
        yamlFilePath,
        "../" + yamlFilePath
    };

    if (!exeDir.empty()) {
        candidates.push_back(exeDir + "/" + yamlFilePath);
    }

    YAML::Node node;
    bool loaded = false;
    for (const auto& path : candidates) {
        try {
            node = YAML::LoadFile(path);
            loaded = true;
            break;
        } catch (...) {
            continue;
        }
    }

    if (!loaded) {
        std::cerr << "YAML file not found: " << yamlFilePath
                  << " (tried: " << yamlFilePath << ", ../" << yamlFilePath;
        if (!exeDir.empty()) {
            std::cerr << ", " << exeDir << "/" << yamlFilePath;
        }
        std::cerr << ")" << std::endl;
        return false;
    }

    // Flatten nested YAML into dot-separated flat keys.
    // Qornix ORM historically used the `db.*` namespace, while some
    // higher-level framework docs/tests use `database.*`. Keep both aliases
    // in sync so either YAML form is accepted consistently.
    auto setConfigValue = [&](const std::string& key, const std::string& value) {
        configValues_[key] = value;

        constexpr const char* dbPrefix = "db.";
        constexpr const char* databasePrefix = "database.";

        if (key.rfind(dbPrefix, 0) == 0) {
            configValues_[std::string(databasePrefix) + key.substr(3)] = value;
        } else if (key.rfind(databasePrefix, 0) == 0) {
            configValues_[std::string(dbPrefix) + key.substr(9)] = value;
        }
    };

    std::function<void(const YAML::Node&, const std::string&)> flatten;
    flatten = [&](const YAML::Node& parent, const std::string& prefix) {
        for (const auto& it : parent) {
            std::string key = it.first.as<std::string>();
            const auto& val = it.second;
            std::string full_key = prefix.empty() ? key : prefix + "." + key;

            if (val.IsMap()) {
                flatten(val, full_key);  // recurse into nested maps
            } else if (val.IsSequence()) {
                // Skip sequences — not used in current config format
            } else if (val.IsScalar() || val.IsDefined()) {
                setConfigValue(full_key, val.as<std::string>());
            }
        }
    };

    flatten(node, "");
    return true;
}
#else
bool ConfigInterface::loadFromYamlFile(const std::string&, const std::string&) {
    std::cerr << "YAML config support not compiled (yaml-cpp not found)" << std::endl;
    return false;
}
#endif

const std::unordered_map<std::string, std::string>& ConfigInterface::getAllConfig() const {
    return configValues_;
}

std::string ConfigInterface::getDbConnectionString() const {
    auto getDbValue = [&](const std::string& name, const std::string& defaultValue = "") {
        const std::string dbKey = "db." + name;
        const std::string databaseKey = "database." + name;
        const std::string value = get(dbKey, "");
        return !value.empty() ? value : get(databaseKey, defaultValue);
    };

    std::string host = getDbValue("host", "localhost");
    std::string port = getDbValue("port", "5432");
    std::string dbName = getDbValue("name", "");
    std::string user = getDbValue("user", "");
    std::string password = getDbValue("password", "");
    std::string driverType = getDbValue("driver", "postgresql");
    std::string explicitConnectionString = getDbValue("connection_string", "");
    std::string sqlitePath = getDbValue("sqlite_path", "");

    if ((driverType == "sqlite" || driverType == "sqlite3") && dbName.empty() && !sqlitePath.empty()) {
        dbName = sqlitePath;
    }

    if (driverType == "sqlite" || driverType == "sqlite3") {
        if (!explicitConnectionString.empty()) {
            return explicitConnectionString;
        }
        if (dbName.empty()) {
            std::cerr << "Required SQLite parameter db.name/database.name, db.sqlite_path/database.sqlite_path or db.connection_string/database.connection_string is missing in config" << std::endl;
            return "";
        }
        return dbName;
    }

    // Check for required parameters
    if (dbName.empty() || user.empty()) {
        std::cerr << "Required database parameters (db.name, db.user) are missing in config" << std::endl;
        return "";
    }

    // Build the connection string depending on the driver type
    std::string connectionString;
    if (driverType == "postgresql") {
        connectionString = "host=" + host + " port=" + port + " dbname=" + dbName +
                         " user=" + user + " password=" + password;
    } else if (driverType == "mysql") {
        connectionString = "host=" + host + ";port=" + port + ";database=" + dbName +
                         ";username=" + user + ";password=" + password;
    } else {
        std::cerr << "Unsupported database driver: " << driverType << std::endl;
        return "";
    }

    return connectionString;
}
