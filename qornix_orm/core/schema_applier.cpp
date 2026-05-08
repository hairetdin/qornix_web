/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_applier.h"

#include <exception>
#include <sstream>

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
            default: out << ch;
        }
    }
    out << '"';
    return out.str();
}

bool containsPlaceholderSql(const std::string& sql) {
    return sql.find("<") != std::string::npos || sql.find("...") != std::string::npos || sql.find("table rebuild") != std::string::npos;
}

bool isCommentOnlySql(const std::string& sql) {
    for (const auto ch : sql) {
        if (ch == '-' || ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

bool SchemaApplyResult::hasFailures() const {
    for (const auto& operation : operations) {
        if (!operation.success && !operation.skipped) {
            return true;
        }
    }
    return false;
}

std::string SchemaApplyResult::toText() const {
    std::ostringstream out;
    out << "Schema apply result for plan " << planId << '\n';
    out << "success=" << (success ? "true" : "false")
        << " dryRun=" << (dryRun ? "true" : "false")
        << " transactionStarted=" << (transactionStarted ? "true" : "false")
        << " committed=" << (committed ? "true" : "false")
        << " rolledBack=" << (rolledBack ? "true" : "false") << '\n';
    if (!message.empty()) {
        out << message << '\n';
    }
    for (const auto& operation : operations) {
        out << "#" << operation.operationId << " " << operation.operationName
            << " executed=" << (operation.executed ? "true" : "false")
            << " skipped=" << (operation.skipped ? "true" : "false")
            << " success=" << (operation.success ? "true" : "false")
            << " :: " << operation.message << '\n';
    }
    return out.str();
}

std::string SchemaApplyResult::toJsonString() const {
    std::ostringstream out;
    out << '{'
        << "\"planId\":" << quoteJson(planId) << ','
        << "\"success\":" << (success ? "true" : "false") << ','
        << "\"dryRun\":" << (dryRun ? "true" : "false") << ','
        << "\"transactionStarted\":" << (transactionStarted ? "true" : "false") << ','
        << "\"committed\":" << (committed ? "true" : "false") << ','
        << "\"rolledBack\":" << (rolledBack ? "true" : "false") << ','
        << "\"message\":" << quoteJson(message) << ','
        << "\"operations\":[";
    for (std::size_t index = 0; index < operations.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        const auto& operation = operations[index];
        out << '{'
            << "\"operationId\":" << operation.operationId << ','
            << "\"operationName\":" << quoteJson(operation.operationName) << ','
            << "\"objectPath\":" << quoteJson(operation.objectPath) << ','
            << "\"executed\":" << (operation.executed ? "true" : "false") << ','
            << "\"skipped\":" << (operation.skipped ? "true" : "false") << ','
            << "\"success\":" << (operation.success ? "true" : "false") << ','
            << "\"message\":" << quoteJson(operation.message)
            << '}';
    }
    out << "]}";
    return out.str();
}

SchemaTransaction::SchemaTransaction(DatabaseInterface& database)
    : database_(database) {}

bool SchemaTransaction::begin(std::string* error) {
    try {
        database_.executeNonQuery("BEGIN");
        active_ = true;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

bool SchemaTransaction::commit(std::string* error) {
    if (!active_) {
        return true;
    }
    try {
        database_.executeNonQuery("COMMIT");
        active_ = false;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

bool SchemaTransaction::rollback(std::string* error) {
    if (!active_) {
        return true;
    }
    try {
        database_.executeNonQuery("ROLLBACK");
        active_ = false;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

bool SchemaTransaction::active() const {
    return active_;
}

bool SchemaApplier::canExecutePlan(const SchemaPlan& plan, const SchemaApplyOptions& options, std::string& error) {
    if (plan.hasUnsupportedOperations() && !options.allowUnsupportedOperations) {
        error = "Plan contains unsupported operations.";
        return false;
    }
    if (plan.hasManualReviewOperations() && !options.manualReviewConfirmed) {
        error = "Plan contains manual-review operations that have not been confirmed.";
        return false;
    }
    if (plan.hasDestructiveOperations() && options.requireConfirmationForDestructive && !options.destructiveConfirmed) {
        error = "Plan contains destructive operations that have not been confirmed.";
        return false;
    }
    if (plan.needsConfirmation() && !options.destructiveConfirmed && options.requireConfirmationForDestructive) {
        error = "Plan requires explicit confirmation.";
        return false;
    }
    return true;
}

SchemaApplyResult SchemaApplier::dryRun(const SchemaPlan& plan) {
    SchemaApplyResult result;
    result.planId = plan.planId;
    result.dryRun = true;
    result.success = true;
    result.message = "Dry-run completed. No database changes were executed.";

    for (const auto& planOperation : plan.operations) {
        SchemaApplyOperationResult operation;
        operation.operationId = planOperation.id;
        operation.operationName = planOperation.operationName;
        operation.objectPath = planOperation.objectPath;
        operation.sql = planOperation.sqlPreview;
        operation.executed = false;
        operation.skipped = true;
        operation.success = true;
        operation.message = "Dry-run preview only.";
        result.operations.push_back(std::move(operation));
    }

    return result;
}

SchemaApplyResult SchemaApplier::applyPlan(
    const SchemaPlan& plan,
    DatabaseInterface& database,
    const SchemaApplyOptions& options
) {
    if (options.dryRun || plan.dryRun) {
        return dryRun(plan);
    }

    SchemaApplyResult result;
    result.planId = plan.planId;
    result.dryRun = false;

    std::string error;
    if (!canExecutePlan(plan, options, error)) {
        result.success = false;
        result.message = error;
        return result;
    }

    SchemaTransaction transaction(database);
    if (options.useTransaction) {
        std::string transactionError;
        if (!transaction.begin(&transactionError)) {
            result.success = false;
            result.message = "Failed to start schema transaction: " + transactionError;
            return result;
        }
        result.transactionStarted = true;
    }

    bool failed = false;
    for (const auto& planOperation : plan.operations) {
        SchemaApplyOperationResult operation;
        operation.operationId = planOperation.id;
        operation.operationName = planOperation.operationName;
        operation.objectPath = planOperation.objectPath;
        operation.sql = planOperation.sqlPreview;

        if (!planOperation.executable || planOperation.unsupported || planOperation.manualReviewRequired) {
            operation.executed = false;
            operation.skipped = true;
            operation.success = true;
            operation.message = "Operation skipped because it is not executable in this plan.";
            result.operations.push_back(std::move(operation));
            continue;
        }

        if (planOperation.sqlPreview.empty() || isCommentOnlySql(planOperation.sqlPreview) || containsPlaceholderSql(planOperation.sqlPreview)) {
            operation.executed = false;
            operation.skipped = true;
            operation.success = true;
            operation.message = "No executable SQL was generated for this operation; SQL preview contains placeholders or comments only.";
            result.operations.push_back(std::move(operation));
            continue;
        }

        try {
            database.executeNonQuery(planOperation.sqlPreview);
            operation.executed = true;
            operation.success = true;
            operation.message = "Executed.";
        } catch (const std::exception& e) {
            operation.executed = false;
            operation.success = false;
            operation.message = e.what();
            failed = true;
            result.operations.push_back(std::move(operation));
            if (options.stopOnFirstError) {
                break;
            }
            continue;
        }

        result.operations.push_back(std::move(operation));
    }

    if (failed) {
        result.success = false;
        result.message = "Schema apply failed.";
        if (transaction.active()) {
            std::string rollbackError;
            result.rolledBack = transaction.rollback(&rollbackError);
            if (!result.rolledBack && !rollbackError.empty()) {
                result.message += " Rollback failed: " + rollbackError;
            }
        }
        return result;
    }

    if (transaction.active()) {
        std::string commitError;
        result.committed = transaction.commit(&commitError);
        if (!result.committed) {
            result.success = false;
            result.message = "Schema apply succeeded but commit failed: " + commitError;
            return result;
        }
    }

    result.success = true;
    result.message = "Schema apply completed.";
    return result;
}
