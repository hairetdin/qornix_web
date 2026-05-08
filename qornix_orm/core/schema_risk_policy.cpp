/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_risk_policy.h"

#include <sstream>
#include <utility>

namespace {

std::string quoteJson(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const auto ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out << "\\u00";
                    const char* hex = "0123456789abcdef";
                    out << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    out << ch;
                }
        }
    }
    out << '"';
    return out.str();
}

bool asBool(const std::string& value) {
    return value == "true" || value == "1" || value == "yes" || value == "TRUE";
}

bool hasValue(const std::string& value) {
    return !value.empty();
}

SchemaRiskAssessment makeAssessment(
    const SchemaDiffOperation& operation,
    SchemaRiskLevel riskLevel,
    std::string reason,
    std::string dataImpact,
    std::string requiredConfirmation,
    std::string recommendedAction
) {
    SchemaRiskAssessment assessment;
    assessment.operation = operation;
    assessment.riskLevel = riskLevel;
    assessment.reason = std::move(reason);
    assessment.dataImpact = std::move(dataImpact);
    assessment.requiredConfirmation = std::move(requiredConfirmation);
    assessment.recommendedAction = std::move(recommendedAction);
    return assessment;
}

SchemaPolicyDecision decisionForRisk(SchemaRiskLevel riskLevel, const SchemaPolicy& policy, bool& executable, bool& requiresConfirmation) {
    executable = false;
    requiresConfirmation = false;

    switch (riskLevel) {
        case SchemaRiskLevel::Safe:
            executable = policy.allowSafe;
            return policy.allowSafe ? SchemaPolicyDecision::Allowed : SchemaPolicyDecision::Blocked;

        case SchemaRiskLevel::Warning:
            if (!policy.allowWarnings) {
                return SchemaPolicyDecision::Blocked;
            }
            executable = !policy.requireConfirmationForWarnings;
            requiresConfirmation = policy.requireConfirmationForWarnings;
            return policy.requireConfirmationForWarnings
                ? SchemaPolicyDecision::RequiresConfirmation
                : SchemaPolicyDecision::Allowed;

        case SchemaRiskLevel::Destructive:
            requiresConfirmation = true;
            executable = policy.allowDestructive && !policy.requireConfirmationForDestructive;
            return policy.allowDestructive && !policy.requireConfirmationForDestructive
                ? SchemaPolicyDecision::Allowed
                : SchemaPolicyDecision::RequiresConfirmation;

        case SchemaRiskLevel::ManualReview:
            executable = policy.allowManualReview;
            return policy.allowManualReview
                ? SchemaPolicyDecision::Allowed
                : SchemaPolicyDecision::ManualReviewRequired;

        case SchemaRiskLevel::Unsupported:
            executable = policy.allowUnsupported;
            return policy.allowUnsupported
                ? SchemaPolicyDecision::Allowed
                : SchemaPolicyDecision::Unsupported;
    }

    return SchemaPolicyDecision::Blocked;
}

SchemaRiskAssessment classifyKind(const SchemaDiffOperation& operation) {
    using Kind = SchemaDiffOperationKind;

    switch (operation.kind) {
        case Kind::TableAdded:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Safe,
                "Table exists only in desired schema and can usually be created without touching existing data.",
                "No existing rows are modified or deleted.",
                "No confirmation required by default.",
                "Planner can generate CREATE TABLE and dependent index/foreign-key operations."
            );

        case Kind::TableOnlyInCurrent:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Destructive,
                "Table exists in the current database but not in desired schema; aligning strictly would require dropping or archiving it.",
                "Dropping the table may delete all rows and dependent objects.",
                "Explicit destructive confirmation is required before a future plan can drop this table.",
                "Review whether the table should be kept, ignored, archived or explicitly dropped."
            );

        case Kind::TableChanged:
            return makeAssessment(
                operation,
                SchemaRiskLevel::ManualReview,
                "Table metadata changed; the safe database operation depends on the concrete property and driver capabilities.",
                "A rename, partitioning change or metadata change may require manual migration steps.",
                "Manual review is required before planning table-level changes.",
                "Inspect the table diff and decide whether it is a rename, metadata-only change or unsupported operation."
            );

        case Kind::ColumnAdded:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Warning,
                "Column exists only in desired schema. It is usually safe, but may fail or require a default if the column is NOT NULL.",
                "Existing rows may need a default value if the new column is non-nullable.",
                "Confirmation may be required if planner detects NOT NULL without default.",
                "Planner should inspect nullable/default metadata before generating ALTER TABLE ADD COLUMN."
            );

        case Kind::ColumnOnlyInCurrent:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Destructive,
                "Column exists in the current database but not in desired schema; aligning strictly would drop the column.",
                "Dropping the column may permanently remove data from every row.",
                "Explicit destructive confirmation is required before a future plan can drop this column.",
                "Review whether the column should be kept, ignored, renamed or explicitly dropped."
            );

        case Kind::ColumnChanged:
            if (operation.property == "type" || operation.property == "maxLength" || operation.property == "precision" || operation.property == "scale") {
                return makeAssessment(
                    operation,
                    SchemaRiskLevel::ManualReview,
                    "Column type/size changed; the operation may require data conversion and is driver-specific.",
                    "Existing values may be truncated, converted incorrectly or rejected.",
                    "Manual review is required before planning type or size changes.",
                    "Inspect current data and driver capabilities before generating ALTER COLUMN operations."
                );
            }
            if (operation.property == "nullable") {
                const bool desiredNullable = asBool(operation.desiredValue);
                return makeAssessment(
                    operation,
                    desiredNullable ? SchemaRiskLevel::Warning : SchemaRiskLevel::ManualReview,
                    desiredNullable
                        ? "Column is becoming nullable; this is usually safe but changes data rules."
                        : "Column is becoming NOT NULL; existing NULL values may make the operation fail.",
                    desiredNullable
                        ? "No existing data should be lost, but future writes become less restrictive."
                        : "Existing NULL values must be cleaned or backfilled before applying the change.",
                    desiredNullable
                        ? "No confirmation required by default."
                        : "Manual review is required before enforcing NOT NULL.",
                    desiredNullable
                        ? "Planner can generate nullable change if the driver supports it."
                        : "Add a default/backfill step or clean data before enforcing NOT NULL."
                );
            }
            if (operation.property == "defaultValue") {
                return makeAssessment(
                    operation,
                    SchemaRiskLevel::Safe,
                    "Default value changed; this normally affects new rows only.",
                    "Existing rows are usually not modified by changing a column default.",
                    "No confirmation required by default.",
                    "Planner can generate ALTER DEFAULT if the driver supports it."
                );
            }
            if (operation.property == "unique") {
                return makeAssessment(
                    operation,
                    SchemaRiskLevel::Warning,
                    "Unique rule changed; enabling uniqueness can fail if duplicate values exist.",
                    "Existing duplicate data may block the operation; disabling uniqueness changes integrity guarantees.",
                    "Review may be required depending on direction and current data.",
                    "Planner should validate duplicates before enabling uniqueness."
                );
            }
            if (operation.property == "primaryKey" || operation.property == "autoIncrement") {
                return makeAssessment(
                    operation,
                    SchemaRiskLevel::ManualReview,
                    "Primary-key or auto-increment metadata changed; this is a structural database change.",
                    "Existing references, row identity and inserts may be affected.",
                    "Manual review is required.",
                    "Plan this as an explicit migration with driver-specific support."
                );
            }
            return makeAssessment(
                operation,
                SchemaRiskLevel::Warning,
                "Column metadata changed.",
                "Data impact depends on the concrete property and driver support.",
                "Review may be required by the planner.",
                "Inspect property-level diff before generating SQL."
            );

        case Kind::PrimaryKeyChanged:
            return makeAssessment(
                operation,
                SchemaRiskLevel::ManualReview,
                "Primary key changed; this affects row identity and relationships.",
                "Existing rows and foreign keys may be affected.",
                "Manual review is required.",
                "Plan primary-key changes explicitly with data checks and rollback strategy."
            );

        case Kind::ForeignKeyAdded:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Warning,
                "Foreign key exists only in desired schema. Adding it may fail if existing data violates the relationship.",
                "No data is deleted, but invalid existing rows may block the operation.",
                "Review may be required if existing data is not validated.",
                "Planner should check referential integrity before adding the foreign key."
            );

        case Kind::ForeignKeyOnlyInCurrent:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Destructive,
                "Foreign key exists in current database but not in desired schema; aligning strictly would drop an integrity constraint.",
                "Data is not deleted, but referential integrity guarantees are removed.",
                "Explicit confirmation is required before dropping constraints.",
                "Review whether the relationship should be removed or kept as database-only metadata."
            );

        case Kind::ForeignKeyChanged:
            return makeAssessment(
                operation,
                SchemaRiskLevel::ManualReview,
                "Foreign key metadata changed; relationship semantics may change.",
                "Existing data and delete/update behavior may be affected.",
                "Manual review is required.",
                "Inspect onDelete/onUpdate/reference changes before planning."
            );

        case Kind::IndexAdded:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Safe,
                "Index exists only in desired schema and can usually be created without modifying data.",
                "Existing rows are not changed; unique indexes may fail if duplicates exist.",
                "No confirmation required by default unless planner detects uniqueness/data issues.",
                "Planner can generate CREATE INDEX after checking index metadata."
            );

        case Kind::IndexOnlyInCurrent:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Warning,
                "Index exists in current database but not in desired schema. Dropping it can affect performance or uniqueness guarantees.",
                "Data is not deleted, but query performance or uniqueness constraints may change.",
                "Review is recommended before dropping indexes.",
                "Keep, ignore or explicitly drop the index after reviewing usage."
            );

        case Kind::IndexChanged:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Warning,
                "Index metadata changed; the database may need to rebuild the index.",
                "Data is not deleted, but writes/queries may be blocked during index rebuild.",
                "Review may be required depending on index size and uniqueness.",
                "Planner should prefer create-new/drop-old strategy where supported."
            );

        case Kind::ViewAdded:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Safe,
                "View exists only in desired schema and can usually be created without changing table data.",
                "No table rows are modified.",
                "No confirmation required by default.",
                "Planner can generate CREATE VIEW after validating dependencies."
            );

        case Kind::ViewOnlyInCurrent:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Destructive,
                "View exists in current database but not in desired schema; aligning strictly would drop it.",
                "No table rows are deleted, but dependent queries/applications may break.",
                "Explicit confirmation is required before dropping database objects.",
                "Review dependencies before dropping the view."
            );

        case Kind::ViewChanged:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Warning,
                "View definition changed; dependent queries may return different data.",
                "Underlying table data is not modified, but read behavior can change.",
                "Review is recommended before replacing a view.",
                "Planner should validate dependencies and generate CREATE OR REPLACE where supported."
            );

        case Kind::TriggerAdded:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Warning,
                "Trigger exists only in desired schema. Adding it changes write-time behavior.",
                "Future INSERT/UPDATE/DELETE operations may produce different side effects.",
                "Review is recommended before adding triggers.",
                "Inspect trigger body and affected table before planning."
            );

        case Kind::TriggerOnlyInCurrent:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Destructive,
                "Trigger exists in current database but not in desired schema; aligning strictly would drop it.",
                "Future write-time behavior may lose side effects or validations.",
                "Explicit confirmation is required before dropping triggers.",
                "Review trigger purpose and dependencies before dropping it."
            );

        case Kind::TriggerChanged:
            return makeAssessment(
                operation,
                SchemaRiskLevel::ManualReview,
                "Trigger definition changed; side effects may change.",
                "Future writes may behave differently.",
                "Manual review is required.",
                "Inspect trigger body and test affected write operations."
            );

        case Kind::SequenceAdded:
        case Kind::SequenceOnlyInCurrent:
        case Kind::SequenceChanged:
            return makeAssessment(
                operation,
                SchemaRiskLevel::Unsupported,
                "Sequence planning is not supported until driver capabilities are introduced.",
                "Sequence changes can affect generated identifiers and inserts.",
                "No automatic execution is allowed.",
                "Handle sequence differences manually until DriverCapabilities and planner support are implemented."
            );
    }

    return makeAssessment(
        operation,
        SchemaRiskLevel::Unsupported,
        "Unknown schema diff operation.",
        "Data impact is unknown.",
        "No automatic execution is allowed.",
        "Update SchemaRiskClassifier to handle this operation explicitly."
    );
}

void applyPolicy(SchemaRiskAssessment& assessment, const SchemaPolicy& policy) {
    assessment.decision = decisionForRisk(
        assessment.riskLevel,
        policy,
        assessment.executable,
        assessment.requiresConfirmation
    );
}

} // namespace

SchemaPolicy SchemaPolicy::defaultPolicy() {
    return SchemaPolicy{};
}

SchemaPolicy SchemaPolicy::reviewPolicy() {
    SchemaPolicy policy;
    policy.requireConfirmationForWarnings = true;
    return policy;
}

SchemaPolicy SchemaPolicy::permissivePolicy() {
    SchemaPolicy policy;
    policy.allowDestructive = true;
    policy.allowManualReview = true;
    policy.allowUnsupported = false;
    policy.requireConfirmationForWarnings = false;
    policy.requireConfirmationForDestructive = true;
    return policy;
}

bool SchemaRiskReport::hasRiskLevel(SchemaRiskLevel riskLevel) const {
    for (const auto& assessment : assessments) {
        if (assessment.riskLevel == riskLevel) {
            return true;
        }
    }
    return false;
}

bool SchemaRiskReport::hasDecision(SchemaPolicyDecision decision) const {
    for (const auto& assessment : assessments) {
        if (assessment.decision == decision) {
            return true;
        }
    }
    return false;
}

bool SchemaRiskReport::hasBlockedOperations() const {
    return hasDecision(SchemaPolicyDecision::Blocked) ||
           hasDecision(SchemaPolicyDecision::RequiresConfirmation) ||
           hasDecision(SchemaPolicyDecision::ManualReviewRequired) ||
           hasDecision(SchemaPolicyDecision::Unsupported);
}

bool SchemaRiskReport::hasDestructiveOperations() const {
    return hasRiskLevel(SchemaRiskLevel::Destructive);
}

bool SchemaRiskReport::hasManualReviewOperations() const {
    return hasRiskLevel(SchemaRiskLevel::ManualReview);
}

bool SchemaRiskReport::hasUnsupportedOperations() const {
    return hasRiskLevel(SchemaRiskLevel::Unsupported);
}

std::vector<SchemaRiskAssessment> SchemaRiskReport::assessmentsByRisk(SchemaRiskLevel riskLevel) const {
    std::vector<SchemaRiskAssessment> result;
    for (const auto& assessment : assessments) {
        if (assessment.riskLevel == riskLevel) {
            result.push_back(assessment);
        }
    }
    return result;
}

std::vector<SchemaRiskAssessment> SchemaRiskReport::assessmentsByDecision(SchemaPolicyDecision decision) const {
    std::vector<SchemaRiskAssessment> result;
    for (const auto& assessment : assessments) {
        if (assessment.decision == decision) {
            result.push_back(assessment);
        }
    }
    return result;
}

std::string SchemaRiskReport::toText() const {
    std::ostringstream out;
    if (assessments.empty()) {
        out << "No schema risk assessments.";
        return out.str();
    }

    out << "Schema risk assessments: " << assessments.size() << '\n';
    for (const auto& assessment : assessments) {
        out << "- "
            << SchemaRiskClassifier::riskLevelToString(assessment.riskLevel)
            << "/"
            << SchemaRiskClassifier::decisionToString(assessment.decision)
            << " "
            << SchemaDiffEngine::operationKindToString(assessment.operation.kind)
            << " "
            << assessment.operation.objectPath;
        if (!assessment.operation.property.empty()) {
            out << " property=" << assessment.operation.property;
        }
        out << " :: " << assessment.reason;
        if (!assessment.requiredConfirmation.empty()) {
            out << " confirmation='" << assessment.requiredConfirmation << "'";
        }
        out << '\n';
    }
    return out.str();
}

std::string SchemaRiskReport::toJsonString() const {
    std::ostringstream out;
    out << "{\"hasAssessments\":" << (hasAssessments() ? "true" : "false")
        << ",\"hasBlockedOperations\":" << (hasBlockedOperations() ? "true" : "false")
        << ",\"assessments\":[";

    for (std::size_t i = 0; i < assessments.size(); ++i) {
        const auto& assessment = assessments[i];
        if (i > 0) {
            out << ',';
        }
        out << '{'
            << "\"kind\":" << quoteJson(SchemaDiffEngine::operationKindToString(assessment.operation.kind)) << ','
            << "\"objectType\":" << quoteJson(assessment.operation.objectType) << ','
            << "\"objectPath\":" << quoteJson(assessment.operation.objectPath) << ','
            << "\"objectName\":" << quoteJson(assessment.operation.objectName) << ','
            << "\"property\":" << quoteJson(assessment.operation.property) << ','
            << "\"riskLevel\":" << quoteJson(SchemaRiskClassifier::riskLevelToString(assessment.riskLevel)) << ','
            << "\"decision\":" << quoteJson(SchemaRiskClassifier::decisionToString(assessment.decision)) << ','
            << "\"executable\":" << (assessment.executable ? "true" : "false") << ','
            << "\"requiresConfirmation\":" << (assessment.requiresConfirmation ? "true" : "false") << ','
            << "\"reason\":" << quoteJson(assessment.reason) << ','
            << "\"dataImpact\":" << quoteJson(assessment.dataImpact) << ','
            << "\"requiredConfirmation\":" << quoteJson(assessment.requiredConfirmation) << ','
            << "\"recommendedAction\":" << quoteJson(assessment.recommendedAction)
            << '}';
    }

    out << "]}";
    return out.str();
}

SchemaRiskAssessment SchemaRiskClassifier::classifyOperation(
    const SchemaDiffOperation& operation,
    const SchemaPolicy& policy
) {
    auto assessment = classifyKind(operation);
    applyPolicy(assessment, policy);
    return assessment;
}

SchemaRiskReport SchemaRiskClassifier::classifyDiff(
    const SchemaDocumentDiff& diff,
    const SchemaPolicy& policy
) {
    SchemaRiskReport report;
    for (const auto& operation : diff.operations) {
        report.assessments.push_back(classifyOperation(operation, policy));
    }
    return report;
}

const char* SchemaRiskClassifier::riskLevelToString(SchemaRiskLevel riskLevel) {
    switch (riskLevel) {
        case SchemaRiskLevel::Safe: return "safe";
        case SchemaRiskLevel::Warning: return "warning";
        case SchemaRiskLevel::Destructive: return "destructive";
        case SchemaRiskLevel::Unsupported: return "unsupported";
        case SchemaRiskLevel::ManualReview: return "manual_review";
    }
    return "unknown";
}

const char* SchemaRiskClassifier::decisionToString(SchemaPolicyDecision decision) {
    switch (decision) {
        case SchemaPolicyDecision::Allowed: return "allowed";
        case SchemaPolicyDecision::RequiresConfirmation: return "requires_confirmation";
        case SchemaPolicyDecision::Blocked: return "blocked";
        case SchemaPolicyDecision::ManualReviewRequired: return "manual_review_required";
        case SchemaPolicyDecision::Unsupported: return "unsupported";
    }
    return "unknown";
}
