/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <filesystem>
#include <vector>
#include "http_server.h"
#include "di_container.h"
#include "extension_interface.h"

class ExtensionLoader {
private:
    struct LoadedExtension {
        void* handle;
        ExtensionInterface* (*createExtension)();
        void (*destroyExtension)(ExtensionInterface*);
        std::unique_ptr<ExtensionInterface> extension;
    };

    std::vector<LoadedExtension> loadedExtensions_;
    std::string extensionsPath_;
    DIContainer& container_;

public:
    ExtensionLoader(const std::string& extensionsPath, DIContainer& container);
    ~ExtensionLoader();

    bool loadExtensions();
    void registerRoutes(HttpServer& server);
    void unloadExtensions();
    void createExtensionsDirectory();
};
