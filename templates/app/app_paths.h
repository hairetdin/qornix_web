/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace qornix_app_paths {

inline bool hasAppLayout(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::exists(root / "templates", ec) &&
           std::filesystem::exists(root / "static", ec) &&
           std::filesystem::exists(root / "config.yaml", ec);
}

inline std::filesystem::path sourceDir() {
#ifdef @PROJECT_NAME_UPPER@_SOURCE_DIR
    return std::filesystem::path(@PROJECT_NAME_UPPER@_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

inline std::filesystem::path executableDir() {
#if defined(_WIN32)
    char buffer[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(buffer).parent_path();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return std::filesystem::absolute(std::filesystem::path(buffer.data())).parent_path();
    }
#else
    char buffer[PATH_MAX];
    ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

inline std::filesystem::path appRoot() {
    if (const char* env = std::getenv("QORNIX_APP_ROOT")) {
        std::filesystem::path candidate(env);
        if (hasAppLayout(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }

    const auto exe = executableDir();
    const auto cwd = std::filesystem::current_path();
    const auto src = sourceDir();

    const std::filesystem::path candidates[] = {
        exe,
        exe.parent_path(),
        cwd,
        cwd.parent_path(),
        src
    };

    for (const auto& candidate : candidates) {
        if (!candidate.empty() && hasAppLayout(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }

    return std::filesystem::absolute(src);
}

inline std::filesystem::path templatesDir() {
    return appRoot() / "templates";
}

inline std::filesystem::path staticDir() {
    return appRoot() / "static";
}

inline std::filesystem::path docDir() {
    return appRoot() / "doc";
}

inline std::filesystem::path templatePath(const std::string& name) {
    return templatesDir() / name;
}

inline std::filesystem::path staticPath(const std::string& name) {
    return staticDir() / name;
}

inline std::filesystem::path docPath(const std::string& name) {
    return docDir() / name;
}

} // namespace qornix_app_paths
