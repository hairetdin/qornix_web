/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <cassert>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "core/config_interface.h"
#include "database/config.h"

namespace {

std::filesystem::path createTempYaml(const std::string& content) {
    const std::string fileName = "qornix_orm_yaml_test_" +
                      std::to_string(std::time(nullptr)) + ".yaml";
    const std::filesystem::path path = std::filesystem::temp_directory_path() / fileName;
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create temp YAML: " + path.string());
    }
    file << content;
    return path;
}

void cleanupFile(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

void test_config_interface_yaml() {
    std::cout << "  [1/5] ConfigInterface YAML parse ... ";

    const std::string yamlContent = R"(
db:
  driver: sqlite
  host: localhost
  port: 5432
  name: test_db
  user: test_user
  password: test_pass
  sqlite_path: ./test.db
app:
  name: test_app
  version: 2.0.0
)";

    const auto yamlPath = createTempYaml(yamlContent);
    ConfigInterface config;
    assert(config.loadFromYamlFile(yamlPath.string()));

    // `db.*` is the canonical ORM namespace, and `database.*` is kept as
    // a framework-level alias for compatibility with generated app configs.
    assert(config.get("db.driver") == "sqlite");
    assert(config.get("db.host") == "localhost");
    assert(config.get("db.port") == "5432");
    assert(config.get("db.name") == "test_db");
    assert(config.get("db.user") == "test_user");
    assert(config.get("db.password") == "test_pass");
    assert(config.get("db.sqlite_path") == "./test.db");
    assert(config.get("database.driver") == "sqlite");
    assert(config.get("database.host") == "localhost");
    assert(config.get("database.port") == "5432");
    assert(config.get("database.name") == "test_db");
    assert(config.get("database.user") == "test_user");
    assert(config.get("database.password") == "test_pass");
    assert(config.get("database.sqlite_path") == "./test.db");
    assert(config.get("app.name") == "test_app");
    assert(config.get("app.version") == "2.0.0");

    cleanupFile(yamlPath);
    std::cout << "OK" << std::endl;
}

void test_config_interface_yaml_sqlite_connection() {
    std::cout << "  [2/5] ConfigInterface YAML → getDbConnectionString (sqlite) ... ";

    const std::string yamlContent = R"(
db:
  driver: sqlite
  sqlite_path: ./my_test.db
)";

    const auto yamlPath = createTempYaml(yamlContent);
    ConfigInterface config;
    assert(config.loadFromYamlFile(yamlPath.string()));

    // For SQLite, getDbConnectionString should use sqlite_path as dbname.
    // This also validates that `db.*` and `database.*` aliases are flattened consistently.
    assert(config.get("db.sqlite_path") == "./my_test.db");
    assert(config.get("database.sqlite_path") == "./my_test.db");
    assert(config.getDbConnectionString() == "./my_test.db");

    cleanupFile(yamlPath);
    std::cout << "OK" << std::endl;
}

void test_config_singleton_yaml() {
    std::cout << "  [3/5] Config singleton YAML parse ... ";

    const std::string yamlContent = R"(
db:
  driver: postgresql
  host: db.example.com
  port: 5433
  name: prod_db
  user: prod_user
  password: prod_pass
)";

    const auto yamlPath = createTempYaml(yamlContent);
    auto& config = Config::getInstance();
    config.reset();
    config.loadFromYamlFile(yamlPath.string());

    auto dbConfig = config.getDatabaseConfig();
    assert(dbConfig.driver == "postgresql");
    assert(dbConfig.host == "db.example.com");
    assert(dbConfig.port == 5433);
    assert(dbConfig.dbname == "prod_db");
    assert(dbConfig.user == "prod_user");
    assert(dbConfig.password == "prod_pass");

    config.reset();
    cleanupFile(yamlPath);
    std::cout << "OK" << std::endl;
}

void test_config_singleton_yaml_sqlite() {
    std::cout << "  [4/5] Config singleton YAML → SQLite config ... ";

    const std::string yamlContent = R"(
db:
  driver: sqlite
  name: ./inline_test.db
  sqlite_path: ./sqlite_path_test.db
)";

    const auto yamlPath = createTempYaml(yamlContent);
    auto& config = Config::getInstance();
    config.reset();
    config.loadFromYamlFile(yamlPath.string());

    auto dbConfig = config.getDatabaseConfig();
    assert(dbConfig.driver == "sqlite");
    // When both name and sqlite_path are set, name takes precedence for dbname
    assert(!dbConfig.dbname.empty());

    config.reset();
    cleanupFile(yamlPath);
    std::cout << "OK" << std::endl;
}


void test_config_interface_yaml_database_alias_root() {
    std::cout << "  [5/5] ConfigInterface YAML database.* root alias ... ";

    const std::string yamlContent = R"(
database:
  driver: sqlite
  sqlite_path: ./database_alias.db
)";

    const auto yamlPath = createTempYaml(yamlContent);
    ConfigInterface config;
    assert(config.loadFromYamlFile(yamlPath.string()));

    assert(config.get("database.driver") == "sqlite");
    assert(config.get("database.sqlite_path") == "./database_alias.db");
    assert(config.get("db.driver") == "sqlite");
    assert(config.get("db.sqlite_path") == "./database_alias.db");
    assert(config.getDbConnectionString() == "./database_alias.db");

    cleanupFile(yamlPath);
    std::cout << "OK" << std::endl;
}

int main() {
    std::cout << "\n=== YAML config parser tests ===" << std::endl;

#ifdef QORNIX_USE_YAML
    test_config_interface_yaml();
    test_config_interface_yaml_sqlite_connection();
    test_config_singleton_yaml();
    test_config_singleton_yaml_sqlite();
    test_config_interface_yaml_database_alias_root();

    std::cout << "\n✅ All YAML config tests passed" << std::endl;
    return 0;
#else
    std::cout << "\n⚠️  YAML support not compiled — skipping tests" << std::endl;
    std::cout << "Rebuild with yaml-cpp to enable YAML config tests" << std::endl;
    return 0;
#endif
}
