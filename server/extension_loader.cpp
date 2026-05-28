/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

// extension_loader.cpp
#include "extension_loader.h"
#include <iostream>
#include <dlfcn.h>

ExtensionLoader::ExtensionLoader(const std::string& extensionsPath, DIContainer& container)
    : extensionsPath_(extensionsPath), container_(container) {
    createExtensionsDirectory();
}

ExtensionLoader::~ExtensionLoader() {
    unloadExtensions();
}

void ExtensionLoader::createExtensionsDirectory() {
    try {
        if (!std::filesystem::exists(extensionsPath_)) {
            std::filesystem::create_directories(extensionsPath_);
            std::cout << "Created extensions directory: " << extensionsPath_ << std::endl;

            // Create example extension
            // std::string exampleDir = extensionsPath_ + "/example";
            // if (!std::filesystem::exists(exampleDir)) {
            //     std::filesystem::create_directories(exampleDir);
            //     std::cout << "Created example extension directory" << std::endl;
            // }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error creating extensions directory: " << e.what() << std::endl;
    }
}

bool ExtensionLoader::loadExtensions() {
    try {
        std::cout << "Loading extensions from dir: " << extensionsPath_ << std::endl;

        // Look for files with suffix _route_extension.so in extensionsPath directory
        for (const auto& entry : std::filesystem::directory_iterator(extensionsPath_)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                // Check if filename ends with "_route_extension.so"
                if (filename.length() >= 19 &&  // Length of "_route_extension.so"
                    filename.substr(filename.length() - 19) == "_route_extension.so") {
                    std::string soPath = entry.path().string();
                    std::cout << "Loading extension from: " << soPath << std::endl;

                    void* handle = dlopen(soPath.c_str(), RTLD_LAZY);
                    if (!handle) {
                        std::cerr << "Failed to load extension " << soPath << ": " << dlerror() << std::endl;
                        continue;
                    }

                    auto createExtension = (ExtensionInterface*(*)())dlsym(handle, "createExtension");
                    auto destroyExtension = (void(*)(ExtensionInterface*))dlsym(handle, "destroyExtension");

                    if (!createExtension || !destroyExtension) {
                        std::cerr << "Extension " << soPath << " missing required functions" << std::endl;
                        dlclose(handle);
                        continue;
                    }

                    ExtensionInterface* raw_extension = createExtension();
                    if (raw_extension) {
                        raw_extension->initialize(container_);

                        LoadedExtension ext;
                        ext.handle = handle;
                        ext.createExtension = createExtension;
                        ext.destroyExtension = destroyExtension;
                        ext.extension = std::unique_ptr<ExtensionInterface>(raw_extension);

                        loadedExtensions_.push_back(std::move(ext));
                        std::cout << "Loaded extension: " << raw_extension->getName() << std::endl;
                    }
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading extensions: " << e.what() << std::endl;
        return false;
    }
}

void ExtensionLoader::registerRoutes(HttpServer& server) {
    for (auto& extension : loadedExtensions_) {
        if (extension.extension) {
            extension.extension->registerRoutes(server, container_);
        }
    }
}

void ExtensionLoader::unloadExtensions() {
    for (auto& extension : loadedExtensions_) {
        if (extension.extension) {
            extension.extension->cleanup();
            if (extension.destroyExtension) {
                extension.destroyExtension(extension.extension.release());
            } else {
                extension.extension.reset();
            }
        }
        if (extension.handle) {
            dlclose(extension.handle);
            extension.handle = nullptr;
        }
    }
    loadedExtensions_.clear();
}
