/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

class DatabaseInterface;

/**
 * Technical metadata about the database snapshot source.
 *
 * Sprint 27 intentionally keeps DatabaseSnapshot as an introspection artifact.
 * It is not the final semantic comparison model. Sprint 28 will convert this
 * snapshot into a canonical SchemaDocument current model so the diff layer can
 * compare SchemaDocument desired vs SchemaDocument current.
 */
struct DatabaseSnapshotMetadata {
    std::string driverName;
    std::string databaseName;
    std::string schemaName;
    std::string connectionName;
    std::string capturedAt;
};

struct DbTable {
    std::string name;
    std::string schemaName;
    std::string type = "table";
    std::string rawSql;
    std::map<std::string, std::string> options;
};

struct DbColumn {
    std::string tableName;
    std::string name;
    std::string type;
    bool nullable = true;
    bool primaryKey = false;
    bool autoIncrement = false;
    bool unique = false;
    std::string defaultValue;
    std::string collation;
    std::string computedExpression;
    int ordinalPosition = 0;
};

struct DbPrimaryKey {
    std::string tableName;
    std::string name;
    std::vector<std::string> columnNames;
};

struct DbForeignKey {
    std::string tableName;
    std::string name;
    std::string columnName;
    std::string referencedTableName;
    std::string referencedColumnName;
    std::string onUpdate;
    std::string onDelete;
    std::string matchType;
    int ordinalPosition = 0;
};

struct DbIndex {
    std::string tableName;
    std::string name;
    std::vector<std::string> columnNames;
    bool unique = false;
    std::string type;
    std::string whereClause;
    bool partial = false;
    std::string rawSql;
};

struct DbConstraint {
    std::string tableName;
    std::string name;
    std::string type;
    std::string expression;
    std::string rawSql;
};

struct DbView {
    std::string name;
    std::string schemaName;
    std::string definition;
    bool materialized = false;
};

struct DbTrigger {
    std::string name;
    std::string tableName;
    std::string event;
    std::string timing;
    std::string definition;
    bool enabled = true;
};

class DatabaseSnapshot {
public:
    DatabaseSnapshotMetadata metadata;
    std::vector<DbTable> tables;
    std::vector<DbColumn> columns;
    std::vector<DbPrimaryKey> primaryKeys;
    std::vector<DbForeignKey> foreignKeys;
    std::vector<DbIndex> indexes;
    std::vector<DbConstraint> constraints;
    std::vector<DbView> views;
    std::vector<DbTrigger> triggers;

    bool empty() const;
    bool hasTable(const std::string& tableName) const;

    std::vector<DbColumn> columnsForTable(const std::string& tableName) const;
    std::vector<DbForeignKey> foreignKeysForTable(const std::string& tableName) const;
    std::vector<DbIndex> indexesForTable(const std::string& tableName) const;

    std::string toDebugString() const;
};

struct DatabaseSnapshotError {
    std::string code;
    std::string message;
    std::string context;
};

struct DatabaseSnapshotResult {
    bool success = false;
    DatabaseSnapshot snapshot;
    std::vector<DatabaseSnapshotError> errors;
    std::vector<std::string> warnings;

    bool ok() const {
        return success;
    }
};

class DatabaseIntrospector {
public:
    static DatabaseSnapshotResult introspect(DatabaseInterface& db);

private:
    static DatabaseSnapshotResult introspectSqlite(DatabaseInterface& db);
    static DatabaseSnapshotResult introspectGeneric(DatabaseInterface& db, const std::string& driverName);
};
