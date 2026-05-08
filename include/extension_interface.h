/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <string>

class ExtensionInterface {
public:
    virtual ~ExtensionInterface() = default;

    virtual std::string getName() const = 0;
    virtual void registerRoutes(HttpServer& server, DIContainer& container) = 0;
    virtual void initialize(DIContainer& container) = 0;
    virtual void cleanup() = 0;
};

typedef ExtensionInterface* (*CreateExtensionFunc)();
typedef void (*DestroyExtensionFunc)(ExtensionInterface*);
