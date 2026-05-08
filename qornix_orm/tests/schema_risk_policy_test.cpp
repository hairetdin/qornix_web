/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "core/schema_risk_policy.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

SchemaDiffOperation op(
    SchemaDiffOperationKind kind,
    const std::string& objectType,
    const std::string& objectPath,
    const std::string& objectName,
    const std::string& property = {},
    const std::string& desiredValue = {},
    const std::string& currentValue = {}
) {
    SchemaDiffOperation operation;
    operation.kind = kind;
    operation.objectType = objectType;
    operation.objectPath = objectPath;
    operation.objectName = objectName;
    operation.property = property;
    operation.desiredValue = desiredValue;
    operation.currentValue = currentValue;
    operation.message = "test operation";
    return operation;
}

void testSafeOperation() {
    const auto assessment = SchemaRiskClassifier::classifyOperation(
        op(SchemaDiffOperationKind::TableAdded, "table", "/entities/products", "products")
    );

    assert(assessment.riskLevel == SchemaRiskLevel::Safe);
    assert(assessment.decision == SchemaPolicyDecision::Allowed);
    assert(assessment.executable);
    assert(!assessment.requiresConfirmation);
    assert(!assessment.reason.empty());
}

void testWarningOperation() {
    const auto assessment = SchemaRiskClassifier::classifyOperation(
        op(SchemaDiffOperationKind::ColumnAdded, "column", "/entities/products/fields/sku", "sku")
    );

    assert(assessment.riskLevel == SchemaRiskLevel::Warning);
    assert(assessment.decision == SchemaPolicyDecision::Allowed);
    assert(assessment.executable);
    assert(!assessment.dataImpact.empty());
}

void testWarningReviewPolicy() {
    const auto assessment = SchemaRiskClassifier::classifyOperation(
        op(SchemaDiffOperationKind::ForeignKeyAdded, "foreign_key", "/entities/products/foreignKeys/category_id", "category_id"),
        SchemaPolicy::reviewPolicy()
    );

    assert(assessment.riskLevel == SchemaRiskLevel::Warning);
    assert(assessment.decision == SchemaPolicyDecision::RequiresConfirmation);
    assert(!assessment.executable);
    assert(assessment.requiresConfirmation);
}

void testDestructiveOperation() {
    const auto assessment = SchemaRiskClassifier::classifyOperation(
        op(SchemaDiffOperationKind::ColumnOnlyInCurrent, "column", "/entities/products/fields/legacy_code", "legacy_code")
    );

    assert(assessment.riskLevel == SchemaRiskLevel::Destructive);
    assert(assessment.decision == SchemaPolicyDecision::RequiresConfirmation);
    assert(!assessment.executable);
    assert(assessment.requiresConfirmation);
    assert(assessment.requiredConfirmation.find("destructive") != std::string::npos ||
           assessment.requiredConfirmation.find("Explicit") != std::string::npos);
}

void testManualReviewOperation() {
    const auto assessment = SchemaRiskClassifier::classifyOperation(
        op(SchemaDiffOperationKind::ColumnChanged, "column", "/entities/products/fields/price", "price", "type", "DECIMAL", "TEXT")
    );

    assert(assessment.riskLevel == SchemaRiskLevel::ManualReview);
    assert(assessment.decision == SchemaPolicyDecision::ManualReviewRequired);
    assert(!assessment.executable);
}

void testUnsupportedOperation() {
    const auto assessment = SchemaRiskClassifier::classifyOperation(
        op(SchemaDiffOperationKind::SequenceChanged, "sequence", "/sequences/order_id", "order_id", "increment", "1", "10")
    );

    assert(assessment.riskLevel == SchemaRiskLevel::Unsupported);
    assert(assessment.decision == SchemaPolicyDecision::Unsupported);
    assert(!assessment.executable);
}

void testReport() {
    SchemaDocumentDiff diff;
    diff.operations.push_back(op(SchemaDiffOperationKind::TableAdded, "table", "/entities/products", "products"));
    diff.operations.push_back(op(SchemaDiffOperationKind::ColumnOnlyInCurrent, "column", "/entities/products/fields/legacy_code", "legacy_code"));
    diff.operations.push_back(op(SchemaDiffOperationKind::TriggerChanged, "trigger", "/triggers/audit_products", "audit_products"));
    diff.operations.push_back(op(SchemaDiffOperationKind::SequenceChanged, "sequence", "/sequences/products_id", "products_id"));

    const auto report = SchemaRiskClassifier::classifyDiff(diff);

    assert(report.hasAssessments());
    assert(report.assessments.size() == 4);
    assert(report.hasRiskLevel(SchemaRiskLevel::Safe));
    assert(report.hasDestructiveOperations());
    assert(report.hasManualReviewOperations());
    assert(report.hasUnsupportedOperations());
    assert(report.hasBlockedOperations());

    const auto destructive = report.assessmentsByRisk(SchemaRiskLevel::Destructive);
    assert(destructive.size() == 1);

    const auto text = report.toText();
    const auto json = report.toJsonString();
    assert(text.find("destructive") != std::string::npos);
    assert(json.find("\"riskLevel\":\"destructive\"") != std::string::npos);
    assert(json.find("\"decision\":\"unsupported\"") != std::string::npos);
}

void testStringConverters() {
    assert(std::string(SchemaRiskClassifier::riskLevelToString(SchemaRiskLevel::Safe)) == "safe");
    assert(std::string(SchemaRiskClassifier::riskLevelToString(SchemaRiskLevel::Warning)) == "warning");
    assert(std::string(SchemaRiskClassifier::riskLevelToString(SchemaRiskLevel::Destructive)) == "destructive");
    assert(std::string(SchemaRiskClassifier::riskLevelToString(SchemaRiskLevel::Unsupported)) == "unsupported");
    assert(std::string(SchemaRiskClassifier::riskLevelToString(SchemaRiskLevel::ManualReview)) == "manual_review");

    assert(std::string(SchemaRiskClassifier::decisionToString(SchemaPolicyDecision::Allowed)) == "allowed");
    assert(std::string(SchemaRiskClassifier::decisionToString(SchemaPolicyDecision::RequiresConfirmation)) == "requires_confirmation");
    assert(std::string(SchemaRiskClassifier::decisionToString(SchemaPolicyDecision::Blocked)) == "blocked");
    assert(std::string(SchemaRiskClassifier::decisionToString(SchemaPolicyDecision::ManualReviewRequired)) == "manual_review_required");
    assert(std::string(SchemaRiskClassifier::decisionToString(SchemaPolicyDecision::Unsupported)) == "unsupported");
}

} // namespace

int main() {
    testSafeOperation();
    testWarningOperation();
    testWarningReviewPolicy();
    testDestructiveOperation();
    testManualReviewOperation();
    testUnsupportedOperation();
    testReport();
    testStringConverters();

    std::cout << "schema_risk_policy_test passed" << std::endl;
    return 0;
}
