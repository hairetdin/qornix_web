/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_plan.h"

#include <algorithm>
#include <iomanip>
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
                    const char* hex = "0123456789abcdef";
                    out << "\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    out << ch;
                }
        }
    }
    out << '"';
    return out.str();
}

std::string safeName(const SchemaDiffOperation& op) {
    if (!op.objectName.empty()) {
        return op.objectName;
    }
    if (!op.objectPath.empty()) {
        return op.objectPath;
    }
    return "unknown_object";
}

std::string commentFor(const SchemaDiffOperation& op) {
    std::ostringstream out;
    out << "-- " << SchemaDiffEngine::operationKindToString(op.kind);
    if (!op.objectPath.empty()) {
        out << " " << op.objectPath;
    }
    if (!op.property.empty()) {
        out << " property=" << op.property;
    }
    return out.str();
}

int orderFor(SchemaCapabilityOperation op) {
    switch (op) {
        case SchemaCapabilityOperation::CreateTable: return 10;
        case SchemaCapabilityOperation::AddColumn: return 20;
        case SchemaCapabilityOperation::AddPrimaryKey: return 30;
        case SchemaCapabilityOperation::AddForeignKey: return 40;
        case SchemaCapabilityOperation::AddIndex: return 50;
        case SchemaCapabilityOperation::CreateView: return 60;
        case SchemaCapabilityOperation::CreateTrigger: return 70;
        case SchemaCapabilityOperation::CreateSequence: return 80;

        case SchemaCapabilityOperation::AlterColumnDefault: return 110;
        case SchemaCapabilityOperation::AlterColumnNullable: return 120;
        case SchemaCapabilityOperation::AlterColumnType: return 130;
        case SchemaCapabilityOperation::RenameTable: return 140;
        case SchemaCapabilityOperation::AlterView: return 150;
        case SchemaCapabilityOperation::AlterTrigger: return 160;

        case SchemaCapabilityOperation::DropForeignKey: return 210;
        case SchemaCapabilityOperation::DropIndex: return 220;
        case SchemaCapabilityOperation::DropPrimaryKey: return 230;
        case SchemaCapabilityOperation::DropColumn: return 240;
        case SchemaCapabilityOperation::DropTrigger: return 250;
        case SchemaCapabilityOperation::DropView: return 260;
        case SchemaCapabilityOperation::DropSequence: return 270;
        case SchemaCapabilityOperation::DropTable: return 280;
        case SchemaCapabilityOperation::Unsupported: return 1000;
    }
    return 1000;
}

std::string placeholderSql(
    const SchemaDiffOperation& diffOperation,
    const SchemaOperationCapability& capability,
    SchemaCapabilityOperation capabilityOperation
) {
    const auto name = safeName(diffOperation);
    std::ostringstream out;
    out << commentFor(diffOperation) << '\n';

    if (!capability.supported) {
        out << "-- Unsupported by selected driver: " << capability.notes;
        return out.str();
    }

    switch (capabilityOperation) {
        case SchemaCapabilityOperation::CreateTable:
            out << "CREATE TABLE " << name << " (\n"
                << "    -- columns are generated from SchemaDocument during full SQL generation\n"
                << ");";
            break;
        case SchemaCapabilityOperation::DropTable:
            out << "DROP TABLE " << name << ";";
            break;
        case SchemaCapabilityOperation::RenameTable:
            out << "ALTER TABLE " << name << " RENAME TO <new_table_name>;";
            break;
        case SchemaCapabilityOperation::AddColumn:
            out << "ALTER TABLE <table_name> ADD COLUMN " << name << " <type>;";
            break;
        case SchemaCapabilityOperation::DropColumn:
            out << "ALTER TABLE <table_name> DROP COLUMN " << name << ";";
            break;
        case SchemaCapabilityOperation::AlterColumnType:
            out << "ALTER TABLE <table_name> ALTER COLUMN " << name << " TYPE <new_type>;";
            break;
        case SchemaCapabilityOperation::AlterColumnNullable:
            out << "ALTER TABLE <table_name> ALTER COLUMN " << name << " SET/DROP NOT NULL;";
            break;
        case SchemaCapabilityOperation::AlterColumnDefault:
            out << "ALTER TABLE <table_name> ALTER COLUMN " << name << " SET/DROP DEFAULT;";
            break;
        case SchemaCapabilityOperation::AddPrimaryKey:
            out << "ALTER TABLE <table_name> ADD PRIMARY KEY (...);";
            break;
        case SchemaCapabilityOperation::DropPrimaryKey:
            out << "ALTER TABLE <table_name> DROP PRIMARY KEY;";
            break;
        case SchemaCapabilityOperation::AddIndex:
            out << "CREATE INDEX " << name << " ON <table_name> (...);";
            break;
        case SchemaCapabilityOperation::DropIndex:
            out << "DROP INDEX " << name << ";";
            break;
        case SchemaCapabilityOperation::AddForeignKey:
            out << "ALTER TABLE <table_name> ADD CONSTRAINT " << name << " FOREIGN KEY (...) REFERENCES <ref_table>(...);";
            break;
        case SchemaCapabilityOperation::DropForeignKey:
            out << "ALTER TABLE <table_name> DROP CONSTRAINT " << name << ";";
            break;
        case SchemaCapabilityOperation::CreateView:
            out << "CREATE VIEW " << name << " AS <select_query>;";
            break;
        case SchemaCapabilityOperation::DropView:
            out << "DROP VIEW " << name << ";";
            break;
        case SchemaCapabilityOperation::AlterView:
            out << capability.sqlPreviewHint;
            break;
        case SchemaCapabilityOperation::CreateTrigger:
            out << "CREATE TRIGGER " << name << " ...;";
            break;
        case SchemaCapabilityOperation::DropTrigger:
            out << "DROP TRIGGER " << name << ";";
            break;
        case SchemaCapabilityOperation::AlterTrigger:
            out << capability.sqlPreviewHint;
            break;
        case SchemaCapabilityOperation::CreateSequence:
            out << "CREATE SEQUENCE " << name << ";";
            break;
        case SchemaCapabilityOperation::DropSequence:
            out << "DROP SEQUENCE " << name << ";";
            break;
        case SchemaCapabilityOperation::Unsupported:
            out << "-- Unsupported operation.";
            break;
    }

    if (capability.requiresTableRebuild) {
        out << "\n-- NOTE: selected driver may require table rebuild for this operation.";
    }
    if (capability.requiresManualReview) {
        out << "\n-- NOTE: manual review is required before execution.";
    }
    return out.str();
}

} // namespace

bool SchemaPlan::hasExecutableOperations() const {
    return std::any_of(operations.begin(), operations.end(), [](const auto& operation) {
        return operation.executable;
    });
}

bool SchemaPlan::hasUnsupportedOperations() const {
    return containsUnsupportedOperations;
}

bool SchemaPlan::hasManualReviewOperations() const {
    return containsManualReviewOperations;
}

bool SchemaPlan::hasDestructiveOperations() const {
    return containsDestructiveOperations;
}

bool SchemaPlan::needsConfirmation() const {
    return requiresConfirmation;
}

std::vector<SchemaPlanOperation> SchemaPlan::executableOperations() const {
    std::vector<SchemaPlanOperation> result;
    for (const auto& operation : operations) {
        if (operation.executable) {
            result.push_back(operation);
        }
    }
    return result;
}

std::vector<SchemaPlanOperation> SchemaPlan::unsupportedOperations() const {
    std::vector<SchemaPlanOperation> result;
    for (const auto& operation : operations) {
        if (operation.unsupported) {
            result.push_back(operation);
        }
    }
    return result;
}

std::vector<SchemaPlanOperation> SchemaPlan::manualReviewOperations() const {
    std::vector<SchemaPlanOperation> result;
    for (const auto& operation : operations) {
        if (operation.manualReviewRequired) {
            result.push_back(operation);
        }
    }
    return result;
}

std::string SchemaPlan::toText() const {
    std::ostringstream out;
    out << "Schema plan " << planId << " for driver " << driverName << '\n';
    out << "dryRun=" << (dryRun ? "true" : "false")
        << " requiresConfirmation=" << (requiresConfirmation ? "true" : "false")
        << " destructive=" << (containsDestructiveOperations ? "true" : "false")
        << " unsupported=" << (containsUnsupportedOperations ? "true" : "false")
        << " manualReview=" << (containsManualReviewOperations ? "true" : "false") << '\n';

    for (const auto& operation : operations) {
        out << "\n#" << operation.id << " " << operation.operationName
            << " " << operation.objectPath << '\n'
            << "risk=" << SchemaRiskClassifier::riskLevelToString(operation.risk.riskLevel)
            << " decision=" << SchemaRiskClassifier::decisionToString(operation.risk.decision)
            << " executable=" << (operation.executable ? "true" : "false")
            << " confirmation=" << (operation.requiresConfirmation ? "true" : "false") << '\n';
        if (!operation.reason.empty()) {
            out << operation.reason << '\n';
        }
        if (!operation.sqlPreview.empty()) {
            out << operation.sqlPreview << '\n';
        }
        if (!operation.manualStep.empty()) {
            out << "Manual step: " << operation.manualStep << '\n';
        }
    }
    return out.str();
}

std::string SchemaPlan::toJsonString() const {
    std::ostringstream out;
    out << '{'
        << "\"planId\":" << quoteJson(planId) << ','
        << "\"driver\":" << quoteJson(driverName) << ','
        << "\"dryRun\":" << (dryRun ? "true" : "false") << ','
        << "\"requiresConfirmation\":" << (requiresConfirmation ? "true" : "false") << ','
        << "\"containsDestructiveOperations\":" << (containsDestructiveOperations ? "true" : "false") << ','
        << "\"containsUnsupportedOperations\":" << (containsUnsupportedOperations ? "true" : "false") << ','
        << "\"containsManualReviewOperations\":" << (containsManualReviewOperations ? "true" : "false") << ','
        << "\"operations\":[";
    for (std::size_t index = 0; index < operations.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        const auto& operation = operations[index];
        out << '{'
            << "\"id\":" << operation.id << ','
            << "\"operation\":" << quoteJson(operation.operationName) << ','
            << "\"objectPath\":" << quoteJson(operation.objectPath) << ','
            << "\"objectName\":" << quoteJson(operation.objectName) << ','
            << "\"risk\":" << quoteJson(SchemaRiskClassifier::riskLevelToString(operation.risk.riskLevel)) << ','
            << "\"decision\":" << quoteJson(SchemaRiskClassifier::decisionToString(operation.risk.decision)) << ','
            << "\"executable\":" << (operation.executable ? "true" : "false") << ','
            << "\"requiresConfirmation\":" << (operation.requiresConfirmation ? "true" : "false") << ','
            << "\"unsupported\":" << (operation.unsupported ? "true" : "false") << ','
            << "\"manualReviewRequired\":" << (operation.manualReviewRequired ? "true" : "false") << ','
            << "\"sqlPreview\":" << quoteJson(operation.sqlPreview) << ','
            << "\"manualStep\":" << quoteJson(operation.manualStep)
            << '}';
    }
    out << "]}";
    return out.str();
}

std::string SchemaPlan::toSqlPreview() const {
    std::ostringstream out;
    out << "-- Schema plan " << planId << " for driver " << driverName << "\n";
    for (const auto& operation : operations) {
        if (!operation.sqlPreview.empty()) {
            out << "\n-- Operation #" << operation.id << "\n" << operation.sqlPreview << "\n";
        }
    }
    return out.str();
}

SchemaPlan SchemaPlanner::buildPlan(
    const SchemaDocumentDiff& diff,
    const DriverCapabilities& capabilities,
    const SchemaPolicy& policy,
    const SchemaPlanOptions& options
) {
    SchemaPlan plan;
    plan.driverName = capabilities.driverName();
    plan.planId = createPlanId(diff, capabilities);
    plan.dryRun = options.dryRun;

    std::size_t id = 1;
    for (const auto& diffOperation : diff.operations) {
        const auto capabilityOperation = DriverCapabilities::operationForDiffOperation(diffOperation);
        const auto capability = capabilities.capabilityFor(capabilityOperation);
        auto risk = SchemaRiskClassifier::classifyOperation(diffOperation, policy);

        if (!capability.supported) {
            risk.riskLevel = SchemaRiskLevel::Unsupported;
            risk.decision = SchemaPolicyDecision::Unsupported;
            risk.executable = false;
            risk.reason = "Selected database driver does not support automatic planning for this operation.";
            risk.recommendedAction = "Review manually or implement driver-specific support.";
        }
        if (capability.requiresManualReview && risk.decision == SchemaPolicyDecision::Allowed) {
            risk.decision = SchemaPolicyDecision::ManualReviewRequired;
            risk.executable = false;
            risk.reason = "Driver capability requires manual review.";
        }

        SchemaPlanOperation operation;
        operation.id = id++;
        operation.diffOperation = diffOperation;
        operation.capabilityOperation = capabilityOperation;
        operation.capability = capability;
        operation.risk = risk;
        operation.objectPath = diffOperation.objectPath;
        operation.objectName = safeName(diffOperation);
        operation.operationName = DriverCapabilities::operationToString(capabilityOperation);
        operation.sqlPreview = options.includeSqlPreview ? buildSqlPreview(diffOperation, capability, capabilityOperation) : std::string{};
        operation.reason = risk.reason;
        operation.requiresConfirmation = risk.requiresConfirmation;
        operation.unsupported = risk.decision == SchemaPolicyDecision::Unsupported || !capability.supported;
        operation.manualReviewRequired = risk.decision == SchemaPolicyDecision::ManualReviewRequired || capability.requiresManualReview;
        operation.executable = capability.supported
            && risk.executable
            && !operation.unsupported
            && !operation.manualReviewRequired;

        if (operation.unsupported && !options.includeUnsupportedOperations) {
            continue;
        }
        if (operation.manualReviewRequired && !options.includeManualReviewOperations) {
            continue;
        }
        if (operation.manualReviewRequired) {
            operation.manualStep = "Review operation, inspect SQL manually and decide whether a driver-specific implementation is required.";
        }

        plan.containsDestructiveOperations = plan.containsDestructiveOperations || risk.riskLevel == SchemaRiskLevel::Destructive || capability.destructive;
        plan.containsUnsupportedOperations = plan.containsUnsupportedOperations || operation.unsupported;
        plan.containsManualReviewOperations = plan.containsManualReviewOperations || operation.manualReviewRequired;
        plan.requiresConfirmation = plan.requiresConfirmation || operation.requiresConfirmation;
        plan.operations.push_back(std::move(operation));
    }

    std::stable_sort(plan.operations.begin(), plan.operations.end(), [](const auto& left, const auto& right) {
        const auto leftOrder = orderFor(left.capabilityOperation);
        const auto rightOrder = orderFor(right.capabilityOperation);
        if (leftOrder != rightOrder) {
            return leftOrder < rightOrder;
        }
        return left.id < right.id;
    });

    for (std::size_t index = 0; index < plan.operations.size(); ++index) {
        plan.operations[index].id = index + 1;
    }

    return plan;
}

std::string SchemaPlanner::buildSqlPreview(
    const SchemaDiffOperation& diffOperation,
    const SchemaOperationCapability& capability,
    SchemaCapabilityOperation capabilityOperation
) {
    return placeholderSql(diffOperation, capability, capabilityOperation);
}

std::string SchemaPlanner::createPlanId(const SchemaDocumentDiff& diff, const DriverCapabilities& capabilities) {
    std::size_t hash = std::hash<std::string>{}(capabilities.driverName());
    for (const auto& operation : diff.operations) {
        hash ^= std::hash<std::string>{}(operation.objectPath + operation.property + operation.desiredValue + operation.currentValue)
            + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    }
    std::ostringstream out;
    out << "plan_" << capabilities.driverName() << '_' << std::hex << hash;
    return out.str();
}
