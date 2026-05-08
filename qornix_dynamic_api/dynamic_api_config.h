/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include "config.h"

#include <cstddef>
#include <string>
#include <vector>

namespace qornix_dynamic_api {

struct DynamicApiConfig {
    DatabaseConfig databaseConfig;

    std::string schemaXsdPath = "qornix_orm/schema/schema_app.xsd";
    std::string uploadedSchemaPath = "uploaded.schema.xml";
    std::string exportedSchemaPath = "database.schema.xml";
    std::string historyPath = "schema_history.jsonl";

    std::string apiPrefix = "/api/dynamic";
    std::string schemaPrefix = "/api/dynamic/schema";

    std::size_t defaultLimit = 100;
    std::size_t maxLimit = 1000;

    bool allowRawFilter = false;
    bool allowRawJoin = false;
    bool allowUpdateWithoutFilter = false;
    bool allowDeleteWithoutFilter = false;
    bool requirePermissions = false;
    bool allowDestructiveSchemaApply = false;

    std::vector<std::string> adminTokens;
};

} // namespace qornix_dynamic_api
