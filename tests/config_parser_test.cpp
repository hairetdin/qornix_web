/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "../include/config_parser.h"
#include <fstream>
#include <iostream>
#include <cassert>
#include <filesystem>

static void test_load_default() {
    ServerSettings settings = ConfigParser::load("nonexistent.yaml");
    assert(settings.address == "127.0.0.1");
    assert(settings.port == 8008);
    assert(settings.enable_logging == false);
    assert(settings.log_level == LogLevel::info);
    std::cout << "✅ test_load_default passed\n";
}

static void test_load_from_file() {
    std::string test_path = "test_config.yaml";
    std::ofstream file(test_path);
    file << R"(
server:
  address: 0.0.0.0
  port: 9000
logging:
  enabled: true
  level: debug
  to_file: true
  file_path: /tmp/test.log
  rotation_size: 20971520
  max_files: 10
)";
    file.close();

    ServerSettings settings = ConfigParser::load(test_path);
    assert(settings.address == "0.0.0.0");
    assert(settings.port == 9000);
    assert(settings.enable_logging == true);
    assert(settings.log_level == LogLevel::debug);
    assert(settings.log_to_file == true);
    assert(settings.log_file_path == "/tmp/test.log");
    assert(settings.log_rotation_size == 20971520);
    assert(settings.log_max_files == 10);

    std::filesystem::remove(test_path);
    std::cout << "✅ test_load_from_file passed\n";
}

static void test_log_level_string() {
    assert(ConfigParser::logLevelToString(LogLevel::trace) == "trace");
    assert(ConfigParser::logLevelToString(LogLevel::debug) == "debug");
    assert(ConfigParser::logLevelToString(LogLevel::info) == "info");
    assert(ConfigParser::logLevelToString(LogLevel::warning) == "warning");
    assert(ConfigParser::logLevelToString(LogLevel::error) == "error");
    assert(ConfigParser::logLevelToString(LogLevel::fatal) == "fatal");
    std::cout << "✅ test_log_level_string passed\n";
}

static void test_string_to_log_level() {
    assert(ConfigParser::stringToLogLevel("trace") == LogLevel::trace);
    assert(ConfigParser::stringToLogLevel("debug") == LogLevel::debug);
    assert(ConfigParser::stringToLogLevel("info") == LogLevel::info);
    assert(ConfigParser::stringToLogLevel("warning") == LogLevel::warning);
    assert(ConfigParser::stringToLogLevel("error") == LogLevel::error);
    assert(ConfigParser::stringToLogLevel("fatal") == LogLevel::fatal);
    assert(ConfigParser::stringToLogLevel("invalid") == LogLevel::info);
    std::cout << "✅ test_string_to_log_level passed\n";
}

int main() {
    test_load_default();
    test_load_from_file();
    test_log_level_string();
    test_string_to_log_level();
    std::cout << "\n🎉 All config_parser tests passed!\n";
    return 0;
}
