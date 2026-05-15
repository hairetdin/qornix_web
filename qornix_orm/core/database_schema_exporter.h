/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "database_snapshot.h"
#include "schema_document.h"

/**
 * Controls how a DatabaseSnapshot is converted into the semantic current
 * SchemaDocument used by future diff/plan/apply code.
 */
struct DatabaseSchemaExportOptions {
    std::string applicationName;
    std::string applicationVersion = "1.0";
    std::string description;
    std::string schemaFormatVersion = "1.0";
    std::string sourceDocumentName = "database_snapshot";
    bool includeRawSqlDescriptions = true;
    bool includeIndexes = true;
    bool includeViews = true;
    bool includeTriggers = true;
};

struct DatabaseSchemaExportError {
    std::string code;
    std::string message;
    std::string context;
};

struct DatabaseSchemaExportResult {
    bool success = false;
    std::unique_ptr<SchemaDocument> document;
    std::vector<DatabaseSchemaExportError> errors;
    std::vector<std::string> warnings;

    bool ok() const {
        return success;
    }
};

/**
 * Converts a technical DatabaseSnapshot into a canonical SchemaDocument.
 *
 * The exporter intentionally keeps the comparison boundary semantic:
 *
 *   user XML -> SchemaDocument desired
 *   database -> DatabaseSnapshot -> SchemaDocument current
 *
 * Future diff code should compare SchemaDocument desired vs SchemaDocument
 * current, not raw XML vs raw database introspection rows.
 */
class DatabaseSchemaExporter {
public:
    static DatabaseSchemaExportResult exportSnapshot(
        const DatabaseSnapshot& snapshot,
        const DatabaseSchemaExportOptions& options = {}
    );

    static std::string exportSnapshotToCanonicalXml(
        const DatabaseSnapshot& snapshot,
        const DatabaseSchemaExportOptions& options = {}
    );

    static bool saveSnapshotAsCanonicalXml(
        const DatabaseSnapshot& snapshot,
        const std::string& outputFilePath,
        const DatabaseSchemaExportOptions& options = {}
    );
};
