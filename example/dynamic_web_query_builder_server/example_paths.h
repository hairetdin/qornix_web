/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <filesystem>
#include <string>

namespace dynamic_web_query_builder_example {

inline std::filesystem::path sourceDir() {
#ifdef DYNAMIC_WEB_QUERY_BUILDER_SERVER_SOURCE_DIR
    return std::filesystem::path(DYNAMIC_WEB_QUERY_BUILDER_SERVER_SOURCE_DIR);
#else
    return std::filesystem::current_path() / "example" / "dynamic_web_query_builder_server";
#endif
}

inline std::string templatesDir() {
    const auto fromRepoRoot = std::filesystem::current_path() /
        "example" / "dynamic_web_query_builder_server" / "templates";
    if (std::filesystem::exists(fromRepoRoot)) {
        return fromRepoRoot.string();
    }

    const auto fromExampleDir = std::filesystem::current_path() / "templates";
    if (std::filesystem::exists(fromExampleDir)) {
        return fromExampleDir.string();
    }

    return (sourceDir() / "templates").string();
}

inline std::filesystem::path demoDatabasePath() {
    return sourceDir() / "dynamic_web_query_builder_demo.sqlite3";
}

inline std::filesystem::path demoSchemaPath() {
    return sourceDir() / "dynamic_web_query_builder_demo.schema.xml";
}

inline std::filesystem::path uploadedSchemaPath() {
    return sourceDir() / "dynamic_web_query_builder_uploaded.schema.xml";
}

} // namespace dynamic_web_query_builder_example
