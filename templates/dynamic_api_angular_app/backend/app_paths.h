#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace dynamic_api_app_paths {

inline std::filesystem::path& executablePathStorage() {
    static std::filesystem::path value;
    return value;
}

inline void setExecutablePath(const char* argv0) {
    if (argv0 != nullptr && *argv0 != '\0') {
        executablePathStorage() = std::filesystem::absolute(std::filesystem::path(argv0));
    }
}

inline bool looksLikeAppRoot(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path / "backend" / "templates", ec)
        && std::filesystem::exists(path / "backend" / "static", ec)
        && std::filesystem::exists(path / "backend" / "schema", ec);
}

inline std::filesystem::path executableDir() {
#if defined(__linux__)
    char buffer[4096];
    const ssize_t len = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len > 0) {
        buffer[len] = '\0';
        return std::filesystem::path(buffer).parent_path();
    }
#endif

    const auto& stored = executablePathStorage();
    if (!stored.empty()) {
        return stored.parent_path();
    }

    return std::filesystem::current_path();
}

inline std::filesystem::path compileTimeSourceDir() {
#ifdef @PROJECT_NAME_UPPER@_SOURCE_DIR
    return std::filesystem::path(@PROJECT_NAME_UPPER@_SOURCE_DIR);
#else
    return {};
#endif
}

inline std::filesystem::path sourceDir() {
    std::vector<std::filesystem::path> candidates;

    if (const char* envRoot = std::getenv("QORNIX_APP_ROOT")) {
        if (*envRoot != '\0') {
            candidates.emplace_back(envRoot);
        }
    }

    const auto exe = executableDir();
    candidates.push_back(exe);
    candidates.push_back(exe / "app");
    candidates.push_back(exe.parent_path());

    const auto cwd = std::filesystem::current_path();
    candidates.push_back(cwd);
    candidates.push_back(cwd.parent_path());

    const auto compiled = compileTimeSourceDir();
    if (!compiled.empty()) {
        candidates.push_back(compiled);
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        const auto absoluteCandidate = std::filesystem::absolute(candidate, ec);
        if (!ec && looksLikeAppRoot(absoluteCandidate)) {
            return absoluteCandidate;
        }
    }

    if (!compiled.empty()) {
        return compiled;
    }
    return cwd;
}

inline std::filesystem::path backendDir() {
    return sourceDir() / "backend";
}

inline std::filesystem::path templatesDir() {
    return backendDir() / "templates";
}

inline std::filesystem::path staticDir() {
    return backendDir() / "static";
}

inline std::filesystem::path docDir() {
    return backendDir() / "doc";
}

inline std::filesystem::path schemaDir() {
    return backendDir() / "schema";
}

inline std::filesystem::path frontendDir() {
    return sourceDir() / "frontend";
}

inline std::filesystem::path frontendDistDir() {
    const auto frontend = frontendDir();
    const auto dist = frontend / "dist";
    std::error_code ec;

    // Source-tree development run: CMake builds Angular into frontend/dist,
    // so the backend must serve the production entry point from there.
    if (std::filesystem::exists(dist / "index.html", ec)) {
        return dist;
    }

    // Portable deploy bundle: CMake copies frontend/dist/* into deploy/frontend/.
    // The deploy bundle does not contain the Angular source tree. Avoid serving
    // the raw source-tree Angular files because they only work behind the Angular dev server.
    if (std::filesystem::exists(frontend / "index.html", ec)
        && !std::filesystem::exists(frontend / "src", ec)) {
        return frontend;
    }

    // Fallback keeps the handler error message actionable when the frontend
    // has not been built yet.
    return dist;
}

inline std::filesystem::path configPath() {
    const auto rootConfig = sourceDir() / "config.yaml";
    std::error_code ec;
    if (std::filesystem::exists(rootConfig, ec)) {
        return rootConfig;
    }
    return backendDir() / "config.yaml";
}

inline std::filesystem::path templatePath(const std::string& name) {
    return templatesDir() / name;
}

inline std::filesystem::path databasePath() {
    return sourceDir() / "app.sqlite3";
}

inline std::filesystem::path uploadedSchemaPath() {
    return schemaDir() / "app.schema.xml";
}

inline std::filesystem::path exportedSchemaPath() {
    return schemaDir() / "database.schema.xml";
}

inline std::filesystem::path historyPath() {
    return schemaDir() / "schema_history.jsonl";
}

inline std::filesystem::path schemaAppXsdPath() {
    const auto schemaCopy = schemaDir() / "schema_app.xsd";
    std::error_code ec;
    if (std::filesystem::exists(schemaCopy, ec)) {
        return schemaCopy;
    }

    const auto staticCopy = staticDir() / "schema_app.xsd";
    if (std::filesystem::exists(staticCopy, ec)) {
        return staticCopy;
    }

#ifdef QORNIX_ORM_SCHEMA_APP_XSD_PATH
    return std::filesystem::path(QORNIX_ORM_SCHEMA_APP_XSD_PATH);
#else
    return schemaCopy;
#endif
}

} // namespace dynamic_api_app_paths
