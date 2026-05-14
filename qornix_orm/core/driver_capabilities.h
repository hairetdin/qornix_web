/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "schema_diff_engine.h"

/**
 * Database driver type used by schema planning.
 *
 * DriverCapabilities only describes what each driver can do. It does not generate SQL,
 * does not plan operations and does not apply changes to a database.
 */
enum class SchemaDatabaseDriver {
    Generic,
    SQLite,
    PostgreSQL,
    MySQL
};

/**
 * Logical DDL operation needed by SchemaPlanner.
 */
enum class SchemaCapabilityOperation {
    CreateTable,
    DropTable,
    RenameTable,

    AddColumn,
    DropColumn,
    AlterColumnType,
    AlterColumnNullable,
    AlterColumnDefault,

    AddPrimaryKey,
    DropPrimaryKey,

    AddIndex,
    DropIndex,

    AddForeignKey,
    DropForeignKey,

    CreateView,
    DropView,
    AlterView,

    CreateTrigger,
    DropTrigger,
    AlterTrigger,

    CreateSequence,
    DropSequence,

    Unsupported
};

struct SchemaOperationCapability {
    SchemaCapabilityOperation operation = SchemaCapabilityOperation::Unsupported;
    std::string name;

    bool supported = false;
    bool requiresTableRebuild = false;
    bool destructive = false;
    bool transactional = false;
    bool requiresLock = false;
    bool requiresManualReview = false;

    std::string sqlPreviewHint;
    std::string notes;
};

class DriverCapabilities {
public:
    DriverCapabilities() = default;

    SchemaDatabaseDriver driver() const;
    const std::string& driverName() const;

    bool supports(SchemaCapabilityOperation operation) const;
    SchemaOperationCapability capabilityFor(SchemaCapabilityOperation operation) const;
    std::vector<SchemaOperationCapability> allCapabilities() const;

    std::string toText() const;
    std::string toJsonString() const;

    static DriverCapabilities generic();
    static DriverCapabilities sqlite();
    static DriverCapabilities postgresql();
    static DriverCapabilities mysql();
    static DriverCapabilities fromDriverName(const std::string& driverName);

    static SchemaCapabilityOperation operationForDiffOperation(const SchemaDiffOperation& operation);

    static const char* driverToString(SchemaDatabaseDriver driver);
    static const char* operationToString(SchemaCapabilityOperation operation);

    DriverCapabilities& set(SchemaOperationCapability capability);

private:
    SchemaDatabaseDriver driver_ = SchemaDatabaseDriver::Generic;
    std::string driverName_ = "generic";
    std::map<SchemaCapabilityOperation, SchemaOperationCapability> capabilities_;

    explicit DriverCapabilities(SchemaDatabaseDriver driver, std::string driverName);

};
