/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <string>
#include <vector>

#include "schema_diff_engine.h"

/**
 * Risk level assigned to a semantic schema diff operation.
 *
 * SchemaRiskPolicy does not plan or execute SQL. It only explains how risky each
 * diff operation is and what policy decision should be made before a future
 * SchemaPlanner/SchemaApplier can process it.
 */
enum class SchemaRiskLevel {
    Safe,
    Warning,
    Destructive,
    Unsupported,
    ManualReview
};

/**
 * Policy decision derived from operation risk and SchemaPolicy.
 */
enum class SchemaPolicyDecision {
    Allowed,
    RequiresConfirmation,
    Blocked,
    ManualReviewRequired,
    Unsupported
};

/**
 * Policy used by SchemaRiskClassifier.
 *
 * Default policy is intentionally conservative but does not block development:
 * safe and warning operations can continue to planning, destructive operations
 * require explicit confirmation, manual-review operations are separated, and
 * unsupported operations are never executable.
 */
struct SchemaPolicy {
    bool allowSafe = true;
    bool allowWarnings = true;
    bool allowDestructive = false;
    bool allowManualReview = false;
    bool allowUnsupported = false;

    bool requireConfirmationForWarnings = false;
    bool requireConfirmationForDestructive = true;

    static SchemaPolicy defaultPolicy();
    static SchemaPolicy reviewPolicy();
    static SchemaPolicy permissivePolicy();
};

struct SchemaRiskAssessment {
    SchemaDiffOperation operation;
    SchemaRiskLevel riskLevel = SchemaRiskLevel::Safe;
    SchemaPolicyDecision decision = SchemaPolicyDecision::Allowed;
    bool executable = true;
    bool requiresConfirmation = false;
    std::string reason;
    std::string dataImpact;
    std::string requiredConfirmation;
    std::string recommendedAction;
};

struct SchemaRiskReport {
    std::vector<SchemaRiskAssessment> assessments;

    bool hasAssessments() const {
        return !assessments.empty();
    }

    bool hasRiskLevel(SchemaRiskLevel riskLevel) const;
    bool hasDecision(SchemaPolicyDecision decision) const;
    bool hasBlockedOperations() const;
    bool hasDestructiveOperations() const;
    bool hasManualReviewOperations() const;
    bool hasUnsupportedOperations() const;

    std::vector<SchemaRiskAssessment> assessmentsByRisk(SchemaRiskLevel riskLevel) const;
    std::vector<SchemaRiskAssessment> assessmentsByDecision(SchemaPolicyDecision decision) const;

    std::string toText() const;
    std::string toJsonString() const;
};

class SchemaRiskClassifier {
public:
    static SchemaRiskAssessment classifyOperation(
        const SchemaDiffOperation& operation,
        const SchemaPolicy& policy = SchemaPolicy::defaultPolicy()
    );

    static SchemaRiskReport classifyDiff(
        const SchemaDocumentDiff& diff,
        const SchemaPolicy& policy = SchemaPolicy::defaultPolicy()
    );

    static const char* riskLevelToString(SchemaRiskLevel riskLevel);
    static const char* decisionToString(SchemaPolicyDecision decision);
};
