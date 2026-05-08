/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "core/driver_capabilities.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

SchemaDiffOperation makeOperation(SchemaDiffOperationKind kind, std::string property = {}) {
    SchemaDiffOperation operation;
    operation.kind = kind;
    operation.objectType = "test";
    operation.objectPath = "/test";
    operation.objectName = "test";
    operation.property = std::move(property);
    return operation;
}

void testDriverLookup() {
    assert(DriverCapabilities::fromDriverName("sqlite").driver() == SchemaDatabaseDriver::SQLite);
    assert(DriverCapabilities::fromDriverName("sqlite3").driver() == SchemaDatabaseDriver::SQLite);
    assert(DriverCapabilities::fromDriverName("postgres").driver() == SchemaDatabaseDriver::PostgreSQL);
    assert(DriverCapabilities::fromDriverName("postgresql").driver() == SchemaDatabaseDriver::PostgreSQL);
    assert(DriverCapabilities::fromDriverName("mysql").driver() == SchemaDatabaseDriver::MySQL);
    assert(DriverCapabilities::fromDriverName("mariadb").driver() == SchemaDatabaseDriver::MySQL);
    assert(DriverCapabilities::fromDriverName("unknown").driver() == SchemaDatabaseDriver::Generic);
}

void testSqliteCapabilities() {
    const auto sqlite = DriverCapabilities::sqlite();

    assert(sqlite.supports(SchemaCapabilityOperation::CreateTable));
    assert(sqlite.supports(SchemaCapabilityOperation::AddColumn));
    assert(sqlite.supports(SchemaCapabilityOperation::CreateView));

    const auto alterType = sqlite.capabilityFor(SchemaCapabilityOperation::AlterColumnType);
    assert(!alterType.supported);
    assert(alterType.requiresTableRebuild);
    assert(alterType.requiresManualReview);

    const auto addFk = sqlite.capabilityFor(SchemaCapabilityOperation::AddForeignKey);
    assert(!addFk.supported);
    assert(addFk.requiresTableRebuild);

    const auto dropTable = sqlite.capabilityFor(SchemaCapabilityOperation::DropTable);
    assert(dropTable.destructive);
}

void testPostgresqlCapabilities() {
    const auto pg = DriverCapabilities::postgresql();

    assert(pg.supports(SchemaCapabilityOperation::CreateTable));
    assert(pg.supports(SchemaCapabilityOperation::AlterColumnType));
    assert(pg.supports(SchemaCapabilityOperation::AddForeignKey));
    assert(pg.supports(SchemaCapabilityOperation::CreateSequence));

    const auto typeChange = pg.capabilityFor(SchemaCapabilityOperation::AlterColumnType);
    assert(typeChange.supported);
    assert(typeChange.transactional);
    assert(typeChange.requiresManualReview);
}

void testMysqlCapabilities() {
    const auto mysql = DriverCapabilities::mysql();

    assert(mysql.supports(SchemaCapabilityOperation::CreateTable));
    assert(mysql.supports(SchemaCapabilityOperation::AlterColumnDefault));
    assert(!mysql.supports(SchemaCapabilityOperation::CreateSequence));

    const auto alterNullable = mysql.capabilityFor(SchemaCapabilityOperation::AlterColumnNullable);
    assert(alterNullable.supported);
    assert(alterNullable.requiresManualReview);
    assert(!alterNullable.transactional);
}

void testDiffOperationMapping() {
    assert(
        DriverCapabilities::operationForDiffOperation(makeOperation(SchemaDiffOperationKind::TableAdded))
        == SchemaCapabilityOperation::CreateTable
    );
    assert(
        DriverCapabilities::operationForDiffOperation(makeOperation(SchemaDiffOperationKind::TableOnlyInCurrent))
        == SchemaCapabilityOperation::DropTable
    );
    assert(
        DriverCapabilities::operationForDiffOperation(makeOperation(SchemaDiffOperationKind::ColumnAdded))
        == SchemaCapabilityOperation::AddColumn
    );
    assert(
        DriverCapabilities::operationForDiffOperation(makeOperation(SchemaDiffOperationKind::ColumnChanged, "type"))
        == SchemaCapabilityOperation::AlterColumnType
    );
    assert(
        DriverCapabilities::operationForDiffOperation(makeOperation(SchemaDiffOperationKind::ColumnChanged, "nullable"))
        == SchemaCapabilityOperation::AlterColumnNullable
    );
    assert(
        DriverCapabilities::operationForDiffOperation(makeOperation(SchemaDiffOperationKind::ColumnChanged, "defaultValue"))
        == SchemaCapabilityOperation::AlterColumnDefault
    );
    assert(
        DriverCapabilities::operationForDiffOperation(makeOperation(SchemaDiffOperationKind::ForeignKeyOnlyInCurrent))
        == SchemaCapabilityOperation::DropForeignKey
    );
    assert(
        DriverCapabilities::operationForDiffOperation(makeOperation(SchemaDiffOperationKind::IndexAdded))
        == SchemaCapabilityOperation::AddIndex
    );
}

void testOutputs() {
    const auto sqlite = DriverCapabilities::sqlite();
    const auto text = sqlite.toText();
    const auto json = sqlite.toJsonString();

    assert(text.find("Driver capabilities: sqlite") != std::string::npos);
    assert(text.find("create_table") != std::string::npos);
    assert(json.find("\"driver\":\"sqlite\"") != std::string::npos);
    assert(json.find("alter_column_type") != std::string::npos);
}

} // namespace

int main() {
    testDriverLookup();
    testSqliteCapabilities();
    testPostgresqlCapabilities();
    testMysqlCapabilities();
    testDiffOperationMapping();
    testOutputs();

    std::cout << "driver_capabilities_test: OK" << std::endl;
    return 0;
}
