/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include <string>
#include <map>
#include <memory>

struct DatabaseConfig {
    std::string host;
    int port;
    std::string dbname;
    std::string user;
    std::string password;
    std::string driver; // "postgresql", "mysql", "sqlite"
    std::string connectionString;
};

class Config {
public:
    static Config &getInstance();

    void loadFromFile(const std::string &filepath);

    void loadFromYamlFile(const std::string &filepath);

    void loadFromEnvironment();

    DatabaseConfig getDatabaseConfig() const;

    void setDatabaseConfig(const DatabaseConfig &config);

    // Other Methods configuration
    std::string get(const std::string &key) const;

    void set(const std::string &key, const std::string &value);

    void loadFromConnectionString(const std::string &connectionString, const std::string& driverType = "postgresql");

    void reset();

private:
    Config() = default;

    ~Config() = default;

    Config(const Config &) = delete;

    Config &operator=(const Config &) = delete;

    DatabaseConfig db_config_;
    std::map<std::string, std::string> settings_;
};

std::string createConnectionString(const DatabaseConfig &config);
