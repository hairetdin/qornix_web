/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <string>
#include <vector>

#include "driver_capabilities.h"
#include "schema_diff_engine.h"
#include "schema_risk_policy.h"

/**
 * A single planned schema operation.
 *
 * SchemaPlanner turns semantic diff operations into an ordered, inspectable plan.
 * The plan is still read-only: it contains SQL preview and metadata, but it
 * does not execute anything by itself.
 */
struct SchemaPlanOperation {
    std::size_t id = 0;

    SchemaDiffOperation diffOperation;
    SchemaCapabilityOperation capabilityOperation = SchemaCapabilityOperation::Unsupported;
    SchemaOperationCapability capability;
    SchemaRiskAssessment risk;

    std::string objectPath;
    std::string objectName;
    std::string operationName;
    std::string sqlPreview;
    std::string manualStep;
    std::string reason;

    bool executable = false;
    bool requiresConfirmation = false;
    bool unsupported = false;
    bool manualReviewRequired = false;
};

struct SchemaPlanOptions {
    bool includeUnsupportedOperations = true;
    bool includeManualReviewOperations = true;
    bool includeSqlPreview = true;
    bool dryRun = true;
};

struct SchemaPlan {
    std::string planId;
    std::string driverName = "generic";
    bool dryRun = true;
    bool containsDestructiveOperations = false;
    bool containsUnsupportedOperations = false;
    bool containsManualReviewOperations = false;
    bool requiresConfirmation = false;

    std::vector<SchemaPlanOperation> operations;

    bool empty() const {
        return operations.empty();
    }

    bool hasExecutableOperations() const;
    bool hasUnsupportedOperations() const;
    bool hasManualReviewOperations() const;
    bool hasDestructiveOperations() const;
    bool needsConfirmation() const;

    std::vector<SchemaPlanOperation> executableOperations() const;
    std::vector<SchemaPlanOperation> unsupportedOperations() const;
    std::vector<SchemaPlanOperation> manualReviewOperations() const;

    std::string toText() const;
    std::string toJsonString() const;
    std::string toSqlPreview() const;
};

/**
 * Converts a semantic SchemaDocumentDiff into a driver-aware SchemaPlan.
 *
 * Input:
 *   - SchemaDocumentDiff from SchemaDiffEngine
 *   - DriverCapabilities
 *   - SchemaPolicy
 *
 * Output:
 *   - ordered operations
 *   - risk labels
 *   - policy decisions
 *   - SQL preview / manual steps
 */
class SchemaPlanner {
public:
    static SchemaPlan buildPlan(
        const SchemaDocumentDiff& diff,
        const DriverCapabilities& capabilities,
        const SchemaPolicy& policy = SchemaPolicy::defaultPolicy(),
        const SchemaPlanOptions& options = SchemaPlanOptions{}
    );

    static std::string buildSqlPreview(
        const SchemaDiffOperation& diffOperation,
        const SchemaOperationCapability& capability,
        SchemaCapabilityOperation capabilityOperation
    );

    static std::string createPlanId(const SchemaDocumentDiff& diff, const DriverCapabilities& capabilities);
};
