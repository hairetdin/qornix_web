/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include <string>
#include <unordered_map>
#include <iostream>

class ConfigInterface {
private:
    std::unordered_map<std::string, std::string> configValues_;

public:
    // Constructor: loads configuration from config.yaml by default
    ConfigInterface();

    // Methods for working with configuration
    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key, const std::string& defaultValue = "") const;

    // Load from file (supports .yaml/.yml and legacy .ini)
    bool loadFromFile(const std::string& configFile = "config.yaml");

    // Load from YAML file (flatten into dot-separated keys)
    // exeDir is the executable path (fallback: next to the binary)
    bool loadFromYamlFile(const std::string& yamlFile, const std::string& exeDir = "");

    // Save to file (legacy flat key=value format)
    bool saveToFile(const std::string& configFile = "config.yaml") const;

    // Print configuration to the console
    void printConfig() const;

    // Get the full configuration
    const std::unordered_map<std::string, std::string>& getAllConfig() const;

    std::string getDbConnectionString() const;
};
