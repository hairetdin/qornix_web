/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <string>
#include <fstream>
#include <yaml-cpp/yaml.h>

enum class LogLevel {
    trace,
    debug,
    info,
    warning,
    error,
    fatal
};

struct ServerSettings {
    std::string address = "127.0.0.1";
    unsigned short port = 8008;
    bool enable_logging = false;
    LogLevel log_level = LogLevel::info;
    bool log_to_file = false;
    std::string log_file_path = "logs/server";
    size_t log_rotation_size = 10 * 1024 * 1024; // 10 MB
    size_t log_max_files = 5;
};

class ConfigParser {
public:
    static bool canLoad(const std::string &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }
        file.close();

        try {
            YAML::LoadFile(path);
            return true;
        } catch (const std::exception &) {
            return false;
        }
    }

    static ServerSettings load(const std::string &path) {
        ServerSettings settings;
        std::ifstream file(path);
        if (!file.is_open()) {
            return settings;
        }
        file.close();

        try {
            YAML::Node node = YAML::LoadFile(path);

            if (node["server"]) {
                const auto &s = node["server"];
                settings.address = s["address"].as<std::string>(settings.address);
                settings.port = s["port"].as<unsigned short>(settings.port);
            }

            if (node["logging"]) {
                const auto &l = node["logging"];
                settings.enable_logging = l["enabled"].as<bool>(settings.enable_logging);
                settings.log_to_file = l["to_file"].as<bool>(settings.log_to_file);
                settings.log_file_path = l["file_path"].as<std::string>(settings.log_file_path);
                settings.log_rotation_size = l["rotation_size"].as<size_t>(settings.log_rotation_size);
                settings.log_max_files = l["max_files"].as<size_t>(settings.log_max_files);

                std::string level_str = l["level"].as<std::string>("info");
                settings.log_level = stringToLogLevel(level_str);
            }
        } catch (const std::exception &e) {
            // Fall back to defaults on parse error.
        }

        return settings;
    }

    static LogLevel stringToLogLevel(const std::string &level) {
        if (level == "trace") return LogLevel::trace;
        if (level == "debug") return LogLevel::debug;
        if (level == "info") return LogLevel::info;
        if (level == "warning") return LogLevel::warning;
        if (level == "error") return LogLevel::error;
        if (level == "fatal") return LogLevel::fatal;
        return LogLevel::info;
    }

    static std::string logLevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::trace: return "trace";
            case LogLevel::debug: return "debug";
            case LogLevel::info: return "info";
            case LogLevel::warning: return "warning";
            case LogLevel::error: return "error";
            case LogLevel::fatal: return "fatal";
            default: return "info";
        }
    }
};
