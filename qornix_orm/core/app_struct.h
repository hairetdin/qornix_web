/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include <string>
#include <vector>
#include "app_struct.h"
#include <map>
#include <climits>

struct ConfigurationDefinition {
    std::string dbConnectionString;
    std::string logLevel;
    int timeout;
    std::string databaseEngine;
};

// Data structures matching the XSD schema
struct FieldDefinition {
    std::string name;
    std::string type;
    bool nullable = false;
    bool primaryKey = false;
    bool unique = false;
    std::string maxLength = "255";
    std::string defaultValue;
    std::string verboseName;
    std::string description;
    bool autoIncrement = false;  // Auto-increment (SERIAL in PostgreSQL, AUTO_INCREMENT in MySQL)
    std::string collation;  // Collation rules
    std::string computedExpression;  // Computed expression (for generated columns)
    bool isComputed = false;  // Whether the field is computed
    std::string precision;  // Precision for numeric types
    std::string scale;  // Scale for numeric types
    std::vector<std::string> enumValues;  // Values for ENUM types (if applicable)
    bool isReverseRelation = false; // Points to the external table for reverse relations
    std::string references;
};

struct ForeignKeyField : public FieldDefinition {
    std::string references;
    std::string onDelete;
    std::string onUpdate;
    std::string toField;
    std::string relatedName;
};

struct IndexDefinition {
    std::string name;
    std::vector<std::string> fieldNames;
    bool unique = false;
    std::string type;
    bool concurrently = false;
    std::string tablespace;
    std::string method;  // Indexing method (btree, hash, gist, gin, etc.)
    std::string whereClause;  // Partial index (WHERE condition)
    std::vector<std::string> operators;  // Operator-class operators
    std::map<std::string, std::string> options;  // Additional index options
    bool isPartial = false;  // Partial index
};

struct CheckConstraintDefinition {
    std::string name;
    std::string expression;
    std::string description;
};

struct ConstraintDefinition {
    std::string name;
    std::string constraintName;
    std::string type; // CHECK, EXCLUDE, UNIQUE
    std::string description;
    std::string expression;
};

struct PartitionBoundDefinition {
    std::string partitionName;
    std::string lowerBound;
    std::string upperBound;
    std::vector<std::string> values;  // For LIST sektsionirovaniyapostgresql://user:password@localhost:5432/mydb<
};

struct PartitionDefinition {
    std::string strategy;  // RANGE, LIST, HASH
    std::vector<std::string> columns;
    std::string expression;
    std::vector<PartitionBoundDefinition> bounds;
};

struct TableTriggerDefinition {
    std::string name;
    std::string event;
    std::string timing;
    std::string functionName;
    std::string condition;
    bool enabled = true;
};

struct SequenceDefinition {
    std::string name;
    std::string tableName;
    std::string columnName;
    long long startValue = 1;
    long long increment = 1;
    long long minValue = 1;
    long long maxValue = LONG_LONG_MAX;
    bool cycle = false;
    long long cache = 1;
};

struct ParameterDefinition {
    std::string name;
    std::string type;
    bool required = false;
    std::string mode;
};

struct FunctionDefinition {
    std::string name;
    std::string description;
    std::vector<ParameterDefinition> parameters;
    std::string returnType;
    std::string code;
    std::string fileName;
    std::string language; // cpp, python, javascript
};

struct EntityDefinition {
    std::string name;
    std::string tableName;
    std::string verboseName;
    std::string verboseNamePlural;
    std::string description;
    std::vector<FieldDefinition> fields;
    std::vector<ForeignKeyField> foreignKeys;
    std::vector<FieldDefinition> reverseRelations;  // For reverse relations
    std::vector<IndexDefinition> indexes;
    std::vector<ConstraintDefinition> constraints;
    std::vector<FunctionDefinition> functions;
    std::vector<CheckConstraintDefinition> checkConstraints;  // CHECK constraints
    std::string tablespace;  // Tablespace (for PostgreSQL)
    std::map<std::string, std::string> options;  // Additional table options
    bool isPartitioned = false;  // Whether the table is partitioned
    std::vector<PartitionDefinition> partitions;  // Partition definitions (if applicable)

};

struct ViewDefinition {
    std::string name;
    std::string viewName;
    std::string description;
    std::string query;
    bool isUpdatable = false;
};

struct MaterializedViewDefinition {
    std::string name;
    std::string viewName;
    std::string description;
    std::string query;
    std::string refreshStrategy;
    bool withData = true;
};

struct StoredProcedureDefinition {
    std::string name;
    std::string procedureName;
    std::string description;
    std::string language;
    std::string security;
    std::string returnType;
    std::string code;
    // Parameters would be added here
};

struct TriggerDefinition {
    std::string name;
    std::string triggerName;
    std::string tableName;
    std::string description;
    std::string event; // INSERT, UPDATE, DELETE, TRUNCATE
    std::string timing; // BEFORE, AFTER, INSTEAD_OF
    std::string function;
    std::string condition;
    bool enabled = true;
};

struct DatabaseFunctionDefinition {
    std::string name;
    std::string functionName;
    std::string description;
    std::string language;
    std::string volatileType;
    std::string security;
    std::string returnType;
    std::string code;
    // Parameters would be added here
};

struct FieldMappingDefinition {
    std::string fieldName;
    std::string columnName;
    std::string postgresqlType;
    std::string mysqlType;
    std::string sqliteType;
};

struct EntityMappingDefinition {
    std::string entityName;
    std::string tableName;
    std::vector<FieldMappingDefinition> fieldMappings;
};

struct TypeMapDefinition {
    std::string appType;
    std::string postgresqlType;
    std::string mysqlType;
    std::string sqliteType;
};

struct TypeMappingDefinition {
    std::vector<TypeMapDefinition> typeMaps;
};

struct DatabaseMappingDefinition {
    std::vector<EntityMappingDefinition> entityMappings;
    TypeMappingDefinition typeMapping;
};

struct SchemaMetadata {
    std::string name;
    std::string version;
    std::string createdAt;
    std::string description;
    std::string author;
    std::string hash; // For change detection
    ConfigurationDefinition configuration;
    DatabaseMappingDefinition databaseMapping;
};

struct SchemaSnapshot {
    SchemaMetadata metadata;
    std::map<std::string, EntityDefinition> entities;
    std::vector<ViewDefinition> views;
    std::vector<MaterializedViewDefinition> materializedViews;
    std::vector<StoredProcedureDefinition> storedProcedures;
    std::vector<TriggerDefinition> triggers;
    std::vector<DatabaseFunctionDefinition> databaseFunctions;
    std::vector<SequenceDefinition> sequences;  // Sequences
    std::vector<TableTriggerDefinition> tableTriggers;  // Table triggers
    std::map<std::string, std::string> extensions;  // Database extensions
    std::vector<PartitionDefinition> partitions;  // Global partition definitions

    // Method for creating a snapshot from current data
    static SchemaSnapshot createFromCurrentSchema(
            const std::map<std::string, EntityDefinition>& entities,
            const std::vector<ViewDefinition>& views,
            const std::vector<MaterializedViewDefinition>& materializedViews,
            const std::vector<StoredProcedureDefinition>& storedProcedures,
            const std::vector<TriggerDefinition>& triggers,
            const std::vector<DatabaseFunctionDefinition>& databaseFunctions
    );

    std::string serializeToJson() const;
};

// Schema comparison result (for future implementation)
struct SchemaDiff {
    struct EntityChanges {
        std::vector<EntityDefinition> added;
        std::vector<EntityDefinition> removed;
        std::map<std::string, std::pair<EntityDefinition, EntityDefinition>> modified;
    };

    struct ViewChanges {
        std::vector<ViewDefinition> added;
        std::vector<ViewDefinition> removed;
        std::map<std::string, std::pair<ViewDefinition, ViewDefinition>> modified;
    };

    // Similarly for other object types
    EntityChanges entityChanges;
    ViewChanges viewChanges;
    // ... other changes

    bool hasChanges() const {
        return !entityChanges.added.empty() ||
               !entityChanges.removed.empty() ||
               !entityChanges.modified.empty() ||
               !viewChanges.added.empty() ||
               !viewChanges.removed.empty() ||
               !viewChanges.modified.empty();
        // Add checks for other change types
    }
};

// Structure to hold schema comparison results
struct EntityDifferences {
    std::vector<std::string> fieldMismatches;
    std::vector<std::string> missingFields;
    std::vector<std::string> extraFields;
    std::vector<std::string> foreignKeyDifferences;
    std::vector<std::string> indexDifferences;
    std::vector<std::string> constraintDifferences;
};

struct ViewDifferences {
    std::vector<std::string> propertyMismatches;  // For query, isUpdatable, etc.
};

struct SchemaComparisonResult {
    bool schemasMatch;
    std::vector<std::string> missingEntities;        // Entities in app schema but not in DB
    std::vector<std::string> extraEntities;          // Entities in DB but not in app schema
    std::map<std::string, EntityDifferences> entityDifferences; // Detailed differences for matching entities

    std::vector<std::string> missingViews;          // Views in app schema but not in DB
    std::vector<std::string> extraViews;            // Views in DB but not in app schema
    std::map<std::string, ViewDifferences> viewDifferences;
};

struct ForeignKeyInfo {
    std::string name;      // Foreign key name in the current table
    std::string references; // Referenced table name
    std::string toField;   // Field in the target table
};