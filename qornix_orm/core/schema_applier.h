/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <string>
#include <vector>

#include "schema_plan.h"
#include "database_interface.h"

/**
 * Options for executing a SchemaPlan.
 *
 * SchemaApplier intentionally requires a SchemaPlan. Applying raw XML directly is
 * not the recommended API. XML must flow through validation, normalization,
 * diff and planning first.
 */
struct SchemaApplyOptions {
    bool dryRun = false;
    bool useTransaction = true;
    bool requireConfirmationForDestructive = true;
    bool destructiveConfirmed = false;
    bool manualReviewConfirmed = false;
    bool allowUnsupportedOperations = false;
    bool stopOnFirstError = true;
};

struct SchemaApplyOperationResult {
    std::size_t operationId = 0;
    std::string operationName;
    std::string objectPath;
    std::string sql;
    bool executed = false;
    bool skipped = false;
    bool success = false;
    std::string message;
};

struct SchemaApplyResult {
    std::string planId;
    bool success = false;
    bool dryRun = false;
    bool transactionStarted = false;
    bool committed = false;
    bool rolledBack = false;
    std::string message;
    std::vector<SchemaApplyOperationResult> operations;

    bool hasFailures() const;
    std::string toText() const;
    std::string toJsonString() const;
};

/**
 * Thin transaction helper for schema DDL flows.
 *
 * Database drivers differ in transactional DDL behavior. The helper is used
 * only when SchemaApplyOptions::useTransaction is true and the caller decides
 * that transaction wrapping is appropriate for the selected driver/plan.
 */
class SchemaTransaction {
public:
    explicit SchemaTransaction(DatabaseInterface& database);

    bool begin(std::string* error = nullptr);
    bool commit(std::string* error = nullptr);
    bool rollback(std::string* error = nullptr);
    bool active() const;

private:
    DatabaseInterface& database_;
    bool active_ = false;
};

class SchemaApplier {
public:
    static SchemaApplyResult applyPlan(
        const SchemaPlan& plan,
        DatabaseInterface& database,
        const SchemaApplyOptions& options = SchemaApplyOptions{}
    );

    static SchemaApplyResult dryRun(const SchemaPlan& plan);

private:
    static bool canExecutePlan(const SchemaPlan& plan, const SchemaApplyOptions& options, std::string& error);
};
