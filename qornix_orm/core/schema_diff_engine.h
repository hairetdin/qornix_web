/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <string>
#include <vector>

#include "schema_document.h"
#include "schema_normalizer.h"

/**
 * Semantic diff kind between desired and current schema documents.
 *
 * Sprint 29 intentionally compares normalized SchemaDocument instances.
 * It does not compare raw user XML to raw database introspection output.
 */
enum class SchemaDiffOperationKind {
    TableAdded,
    TableOnlyInCurrent,
    TableChanged,

    ColumnAdded,
    ColumnOnlyInCurrent,
    ColumnChanged,

    PrimaryKeyChanged,

    ForeignKeyAdded,
    ForeignKeyOnlyInCurrent,
    ForeignKeyChanged,

    IndexAdded,
    IndexOnlyInCurrent,
    IndexChanged,

    ViewAdded,
    ViewOnlyInCurrent,
    ViewChanged,

    TriggerAdded,
    TriggerOnlyInCurrent,
    TriggerChanged,

    SequenceAdded,
    SequenceOnlyInCurrent,
    SequenceChanged
};

struct SchemaDiffOperation {
    SchemaDiffOperationKind kind = SchemaDiffOperationKind::TableChanged;
    std::string objectType;
    std::string objectPath;
    std::string objectName;
    std::string property;
    std::string desiredValue;
    std::string currentValue;
    std::string message;
};

struct SchemaDocumentDiff {
    std::vector<SchemaDiffOperation> operations;

    bool hasChanges() const {
        return !operations.empty();
    }

    std::vector<SchemaDiffOperation> operationsByKind(SchemaDiffOperationKind kind) const;
    std::string toText() const;
    std::string toJsonString() const;
};

struct SchemaDiffOptions {
    /**
     * Normalize both documents before comparing. This is enabled by default so
     * valid user XML and DB-exported XML are compared by semantics rather than
     * by element order, formatting or trivial aliases.
     */
    bool normalizeBeforeCompare = true;
    SchemaNormalizationOptions normalizationOptions;

    /**
     * Compare raw SQL definitions for views/triggers. Disable this only for
     * very early debugging of driver-specific introspection output.
     */
    bool compareSqlDefinitions = true;
};

/**
 * Compares two SchemaDocument semantic models:
 *
 *   desired SchemaDocument: user intent loaded from XSD-valid XML
 *   current SchemaDocument: current DB state exported from DatabaseSnapshot
 *
 * The engine is read-only: it does not modify XML, SchemaDocument instances or
 * the database. Sprint 30 will classify risk. Sprint 32 will turn diff into a
 * plan. Sprint 33 will apply confirmed plans.
 */
class SchemaDiffEngine {
public:
    static SchemaDocumentDiff compare(
        const SchemaDocument& desired,
        const SchemaDocument& current,
        const SchemaDiffOptions& options = SchemaDiffOptions{}
    );

    static const char* operationKindToString(SchemaDiffOperationKind kind);
};
