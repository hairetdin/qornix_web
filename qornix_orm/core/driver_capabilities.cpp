/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "driver_capabilities.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

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

SchemaOperationCapability makeCapability(
    SchemaCapabilityOperation operation,
    bool supported,
    bool requiresTableRebuild,
    bool destructive,
    bool transactional,
    bool requiresLock,
    bool requiresManualReview,
    std::string sqlPreviewHint,
    std::string notes
) {
    SchemaOperationCapability capability;
    capability.operation = operation;
    capability.name = DriverCapabilities::operationToString(operation);
    capability.supported = supported;
    capability.requiresTableRebuild = requiresTableRebuild;
    capability.destructive = destructive;
    capability.transactional = transactional;
    capability.requiresLock = requiresLock;
    capability.requiresManualReview = requiresManualReview;
    capability.sqlPreviewHint = std::move(sqlPreviewHint);
    capability.notes = std::move(notes);
    return capability;
}

void addCommonCapabilities(
    DriverCapabilities& capabilities,
    bool ddlTransactional,
    bool supportsSequences
) {
    capabilities
        .set(makeCapability(SchemaCapabilityOperation::CreateTable, true, false, false, ddlTransactional, true, false, "CREATE TABLE ...", "Creates a new table."))
        .set(makeCapability(SchemaCapabilityOperation::DropTable, true, false, true, ddlTransactional, true, true, "DROP TABLE ...", "Destructive: removes table data and dependent objects."))
        .set(makeCapability(SchemaCapabilityOperation::RenameTable, true, false, false, ddlTransactional, true, true, "ALTER TABLE ... RENAME TO ...", "May require foreign-key/index review."))
        .set(makeCapability(SchemaCapabilityOperation::AddColumn, true, false, false, ddlTransactional, true, false, "ALTER TABLE ... ADD COLUMN ...", "Safe when nullable or default is provided."))
        .set(makeCapability(SchemaCapabilityOperation::DropColumn, true, false, true, ddlTransactional, true, true, "ALTER TABLE ... DROP COLUMN ...", "Destructive: removes data from every row."))
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnType, true, false, false, ddlTransactional, true, true, "ALTER TABLE ... ALTER COLUMN ... TYPE ...", "May require data conversion and validation."))
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnNullable, true, false, false, ddlTransactional, true, true, "ALTER TABLE ... ALTER COLUMN ... SET/DROP NOT NULL", "Enforcing NOT NULL requires existing data checks."))
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnDefault, true, false, false, ddlTransactional, true, false, "ALTER TABLE ... ALTER COLUMN ... SET/DROP DEFAULT", "Usually affects new rows only."))
        .set(makeCapability(SchemaCapabilityOperation::AddPrimaryKey, true, false, false, ddlTransactional, true, true, "ALTER TABLE ... ADD PRIMARY KEY ...", "Requires uniqueness and NOT NULL checks."))
        .set(makeCapability(SchemaCapabilityOperation::DropPrimaryKey, true, false, true, ddlTransactional, true, true, "ALTER TABLE ... DROP PRIMARY KEY", "Destructive for integrity semantics."))
        .set(makeCapability(SchemaCapabilityOperation::AddIndex, true, false, false, ddlTransactional, true, false, "CREATE INDEX ...", "May lock or scan table depending on driver."))
        .set(makeCapability(SchemaCapabilityOperation::DropIndex, true, false, true, ddlTransactional, true, true, "DROP INDEX ...", "Removes performance/integrity metadata."))
        .set(makeCapability(SchemaCapabilityOperation::AddForeignKey, true, false, false, ddlTransactional, true, true, "ALTER TABLE ... ADD CONSTRAINT ... FOREIGN KEY ...", "Requires referential integrity checks."))
        .set(makeCapability(SchemaCapabilityOperation::DropForeignKey, true, false, true, ddlTransactional, true, true, "ALTER TABLE ... DROP CONSTRAINT ...", "Removes referential integrity guarantee."))
        .set(makeCapability(SchemaCapabilityOperation::CreateView, true, false, false, ddlTransactional, false, false, "CREATE VIEW ...", "Creates a database view."))
        .set(makeCapability(SchemaCapabilityOperation::DropView, true, false, true, ddlTransactional, false, true, "DROP VIEW ...", "May break consumers of the view."))
        .set(makeCapability(SchemaCapabilityOperation::AlterView, true, false, false, ddlTransactional, false, true, "CREATE OR REPLACE VIEW ...", "Requires SQL definition review."))
        .set(makeCapability(SchemaCapabilityOperation::CreateTrigger, true, false, false, ddlTransactional, true, true, "CREATE TRIGGER ...", "Trigger body requires manual review."))
        .set(makeCapability(SchemaCapabilityOperation::DropTrigger, true, false, true, ddlTransactional, true, true, "DROP TRIGGER ...", "Removes automatic database behavior."))
        .set(makeCapability(SchemaCapabilityOperation::AlterTrigger, true, false, false, ddlTransactional, true, true, "DROP TRIGGER ...; CREATE TRIGGER ...", "Usually implemented as drop/create and needs review."))
        .set(makeCapability(SchemaCapabilityOperation::CreateSequence, supportsSequences, false, false, ddlTransactional, false, false, "CREATE SEQUENCE ...", supportsSequences ? "Creates a sequence." : "Driver does not expose standalone sequences."))
        .set(makeCapability(SchemaCapabilityOperation::DropSequence, supportsSequences, false, true, ddlTransactional, false, true, "DROP SEQUENCE ...", supportsSequences ? "Drops a sequence." : "Driver does not expose standalone sequences."))
        .set(makeCapability(SchemaCapabilityOperation::Unsupported, false, false, false, false, false, true, "", "No automatic operation is available."));
}

} // namespace

DriverCapabilities::DriverCapabilities(SchemaDatabaseDriver driver, std::string driverName)
    : driver_(driver), driverName_(std::move(driverName)) {}

DriverCapabilities& DriverCapabilities::set(SchemaOperationCapability capability) {
    capabilities_[capability.operation] = std::move(capability);
    return *this;
}

SchemaDatabaseDriver DriverCapabilities::driver() const {
    return driver_;
}

const std::string& DriverCapabilities::driverName() const {
    return driverName_;
}

bool DriverCapabilities::supports(SchemaCapabilityOperation operation) const {
    return capabilityFor(operation).supported;
}

SchemaOperationCapability DriverCapabilities::capabilityFor(SchemaCapabilityOperation operation) const {
    const auto it = capabilities_.find(operation);
    if (it != capabilities_.end()) {
        return it->second;
    }
    return makeCapability(operation, false, false, false, false, false, true, "", "Capability is not defined for this driver.");
}

std::vector<SchemaOperationCapability> DriverCapabilities::allCapabilities() const {
    std::vector<SchemaOperationCapability> result;
    for (const auto& item : capabilities_) {
        result.push_back(item.second);
    }
    return result;
}

std::string DriverCapabilities::toText() const {
    std::ostringstream out;
    out << "Driver capabilities: " << driverName_ << '\n';
    for (const auto& item : capabilities_) {
        const auto& capability = item.second;
        out << "- " << operationToString(capability.operation)
            << " supported=" << (capability.supported ? "true" : "false")
            << " destructive=" << (capability.destructive ? "true" : "false")
            << " transactional=" << (capability.transactional ? "true" : "false")
            << " tableRebuild=" << (capability.requiresTableRebuild ? "true" : "false")
            << " manualReview=" << (capability.requiresManualReview ? "true" : "false")
            << " :: " << capability.notes << '\n';
    }
    return out.str();
}

std::string DriverCapabilities::toJsonString() const {
    std::ostringstream out;
    out << "{\"driver\":" << quoteJson(driverName_) << ",\"capabilities\":[";
    std::size_t index = 0;
    for (const auto& item : capabilities_) {
        const auto& capability = item.second;
        if (index++ > 0) {
            out << ',';
        }
        out << '{'
            << "\"operation\":" << quoteJson(operationToString(capability.operation)) << ','
            << "\"supported\":" << (capability.supported ? "true" : "false") << ','
            << "\"requiresTableRebuild\":" << (capability.requiresTableRebuild ? "true" : "false") << ','
            << "\"destructive\":" << (capability.destructive ? "true" : "false") << ','
            << "\"transactional\":" << (capability.transactional ? "true" : "false") << ','
            << "\"requiresLock\":" << (capability.requiresLock ? "true" : "false") << ','
            << "\"requiresManualReview\":" << (capability.requiresManualReview ? "true" : "false") << ','
            << "\"sqlPreviewHint\":" << quoteJson(capability.sqlPreviewHint) << ','
            << "\"notes\":" << quoteJson(capability.notes)
            << '}';
    }
    out << "]}";
    return out.str();
}

DriverCapabilities DriverCapabilities::generic() {
    DriverCapabilities capabilities(SchemaDatabaseDriver::Generic, "generic");
    addCommonCapabilities(capabilities, false, false);

    for (auto capability : capabilities.allCapabilities()) {
        capability.supported = false;
        capability.requiresManualReview = true;
        capability.transactional = false;
        capability.notes = "Generic driver does not allow automatic planning for this operation.";
        capabilities.set(capability);
    }

    capabilities.set(makeCapability(SchemaCapabilityOperation::Unsupported, false, false, false, false, false, true, "", "Unsupported operation."));
    return capabilities;
}

DriverCapabilities DriverCapabilities::sqlite() {
    DriverCapabilities capabilities(SchemaDatabaseDriver::SQLite, "sqlite");
    addCommonCapabilities(capabilities, true, false);

    capabilities
        .set(makeCapability(SchemaCapabilityOperation::DropColumn, true, true, true, true, true, true, "ALTER TABLE ... DROP COLUMN ... or table rebuild", "SQLite support depends on version and constraints; planner should be conservative."))
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnType, false, true, false, true, true, true, "table rebuild", "SQLite does not support a simple ALTER COLUMN TYPE operation."))
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnNullable, false, true, false, true, true, true, "table rebuild", "SQLite usually requires table rebuild for nullable changes."))
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnDefault, false, true, false, true, true, true, "table rebuild", "SQLite usually requires table rebuild for changing defaults."))
        .set(makeCapability(SchemaCapabilityOperation::AddPrimaryKey, false, true, false, true, true, true, "table rebuild", "SQLite cannot add a primary key with a simple ALTER TABLE."))
        .set(makeCapability(SchemaCapabilityOperation::DropPrimaryKey, false, true, true, true, true, true, "table rebuild", "SQLite primary-key changes require table rebuild."))
        .set(makeCapability(SchemaCapabilityOperation::AddForeignKey, false, true, false, true, true, true, "table rebuild", "SQLite foreign-key additions usually require table rebuild."))
        .set(makeCapability(SchemaCapabilityOperation::DropForeignKey, false, true, true, true, true, true, "table rebuild", "SQLite foreign-key removals usually require table rebuild."))
        .set(makeCapability(SchemaCapabilityOperation::AlterView, true, false, false, true, false, true, "DROP VIEW ...; CREATE VIEW ...", "SQLite view changes are implemented as drop/create."))
        .set(makeCapability(SchemaCapabilityOperation::AlterTrigger, true, false, false, true, true, true, "DROP TRIGGER ...; CREATE TRIGGER ...", "SQLite trigger changes are implemented as drop/create."))
        .set(makeCapability(SchemaCapabilityOperation::CreateSequence, false, false, false, false, false, true, "", "SQLite uses AUTOINCREMENT metadata instead of standalone sequences."))
        .set(makeCapability(SchemaCapabilityOperation::DropSequence, false, false, true, false, false, true, "", "SQLite uses AUTOINCREMENT metadata instead of standalone sequences."));

    return capabilities;
}

DriverCapabilities DriverCapabilities::postgresql() {
    DriverCapabilities capabilities(SchemaDatabaseDriver::PostgreSQL, "postgresql");
    addCommonCapabilities(capabilities, true, true);
    capabilities
        .set(makeCapability(SchemaCapabilityOperation::AlterView, true, false, false, true, true, true, "CREATE OR REPLACE VIEW ...", "Review dependent objects before replacing a view."))
        .set(makeCapability(SchemaCapabilityOperation::AlterTrigger, true, false, false, true, true, true, "CREATE OR REPLACE FUNCTION ...; DROP/CREATE TRIGGER ...", "Trigger/function changes require manual review."));
    return capabilities;
}

DriverCapabilities DriverCapabilities::mysql() {
    DriverCapabilities capabilities(SchemaDatabaseDriver::MySQL, "mysql");
    addCommonCapabilities(capabilities, false, false);
    capabilities
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnType, true, false, false, false, true, true, "ALTER TABLE ... MODIFY COLUMN ...", "May rebuild/lock table depending on engine/version."))
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnNullable, true, false, false, false, true, true, "ALTER TABLE ... MODIFY COLUMN ...", "Requires full column definition in MySQL."))
        .set(makeCapability(SchemaCapabilityOperation::AlterColumnDefault, true, false, false, false, true, false, "ALTER TABLE ... ALTER COLUMN ... SET/DROP DEFAULT", "Usually affects new rows only."))
        .set(makeCapability(SchemaCapabilityOperation::CreateSequence, false, false, false, false, false, true, "", "Most MySQL applications use AUTO_INCREMENT instead of standalone sequences."))
        .set(makeCapability(SchemaCapabilityOperation::DropSequence, false, false, true, false, false, true, "", "Most MySQL applications use AUTO_INCREMENT instead of standalone sequences."));
    return capabilities;
}

DriverCapabilities DriverCapabilities::fromDriverName(const std::string& driverName) {
    const auto value = lower(driverName);
    if (value == "sqlite" || value == "sqlite3") {
        return sqlite();
    }
    if (value == "postgres" || value == "postgresql" || value == "pg") {
        return postgresql();
    }
    if (value == "mysql" || value == "mariadb") {
        return mysql();
    }
    return generic();
}

SchemaCapabilityOperation DriverCapabilities::operationForDiffOperation(const SchemaDiffOperation& operation) {
    using Kind = SchemaDiffOperationKind;

    switch (operation.kind) {
        case Kind::TableAdded: return SchemaCapabilityOperation::CreateTable;
        case Kind::TableOnlyInCurrent: return SchemaCapabilityOperation::DropTable;
        case Kind::TableChanged: return SchemaCapabilityOperation::RenameTable;

        case Kind::ColumnAdded: return SchemaCapabilityOperation::AddColumn;
        case Kind::ColumnOnlyInCurrent: return SchemaCapabilityOperation::DropColumn;
        case Kind::ColumnChanged:
            if (operation.property == "type" || operation.property == "maxLength" || operation.property == "precision" || operation.property == "scale") {
                return SchemaCapabilityOperation::AlterColumnType;
            }
            if (operation.property == "nullable") {
                return SchemaCapabilityOperation::AlterColumnNullable;
            }
            if (operation.property == "defaultValue") {
                return SchemaCapabilityOperation::AlterColumnDefault;
            }
            if (operation.property == "primaryKey" || operation.property == "autoIncrement") {
                return SchemaCapabilityOperation::AddPrimaryKey;
            }
            return SchemaCapabilityOperation::AlterColumnType;

        case Kind::PrimaryKeyChanged: return SchemaCapabilityOperation::AddPrimaryKey;

        case Kind::ForeignKeyAdded: return SchemaCapabilityOperation::AddForeignKey;
        case Kind::ForeignKeyOnlyInCurrent: return SchemaCapabilityOperation::DropForeignKey;
        case Kind::ForeignKeyChanged: return SchemaCapabilityOperation::AddForeignKey;

        case Kind::IndexAdded: return SchemaCapabilityOperation::AddIndex;
        case Kind::IndexOnlyInCurrent: return SchemaCapabilityOperation::DropIndex;
        case Kind::IndexChanged: return SchemaCapabilityOperation::AddIndex;

        case Kind::ViewAdded: return SchemaCapabilityOperation::CreateView;
        case Kind::ViewOnlyInCurrent: return SchemaCapabilityOperation::DropView;
        case Kind::ViewChanged: return SchemaCapabilityOperation::AlterView;

        case Kind::TriggerAdded: return SchemaCapabilityOperation::CreateTrigger;
        case Kind::TriggerOnlyInCurrent: return SchemaCapabilityOperation::DropTrigger;
        case Kind::TriggerChanged: return SchemaCapabilityOperation::AlterTrigger;

        case Kind::SequenceAdded: return SchemaCapabilityOperation::CreateSequence;
        case Kind::SequenceOnlyInCurrent: return SchemaCapabilityOperation::DropSequence;
        case Kind::SequenceChanged: return SchemaCapabilityOperation::CreateSequence;
    }

    return SchemaCapabilityOperation::Unsupported;
}

const char* DriverCapabilities::driverToString(SchemaDatabaseDriver driver) {
    switch (driver) {
        case SchemaDatabaseDriver::Generic: return "generic";
        case SchemaDatabaseDriver::SQLite: return "sqlite";
        case SchemaDatabaseDriver::PostgreSQL: return "postgresql";
        case SchemaDatabaseDriver::MySQL: return "mysql";
    }
    return "generic";
}

const char* DriverCapabilities::operationToString(SchemaCapabilityOperation operation) {
    switch (operation) {
        case SchemaCapabilityOperation::CreateTable: return "create_table";
        case SchemaCapabilityOperation::DropTable: return "drop_table";
        case SchemaCapabilityOperation::RenameTable: return "rename_table";
        case SchemaCapabilityOperation::AddColumn: return "add_column";
        case SchemaCapabilityOperation::DropColumn: return "drop_column";
        case SchemaCapabilityOperation::AlterColumnType: return "alter_column_type";
        case SchemaCapabilityOperation::AlterColumnNullable: return "alter_column_nullable";
        case SchemaCapabilityOperation::AlterColumnDefault: return "alter_column_default";
        case SchemaCapabilityOperation::AddPrimaryKey: return "add_primary_key";
        case SchemaCapabilityOperation::DropPrimaryKey: return "drop_primary_key";
        case SchemaCapabilityOperation::AddIndex: return "add_index";
        case SchemaCapabilityOperation::DropIndex: return "drop_index";
        case SchemaCapabilityOperation::AddForeignKey: return "add_foreign_key";
        case SchemaCapabilityOperation::DropForeignKey: return "drop_foreign_key";
        case SchemaCapabilityOperation::CreateView: return "create_view";
        case SchemaCapabilityOperation::DropView: return "drop_view";
        case SchemaCapabilityOperation::AlterView: return "alter_view";
        case SchemaCapabilityOperation::CreateTrigger: return "create_trigger";
        case SchemaCapabilityOperation::DropTrigger: return "drop_trigger";
        case SchemaCapabilityOperation::AlterTrigger: return "alter_trigger";
        case SchemaCapabilityOperation::CreateSequence: return "create_sequence";
        case SchemaCapabilityOperation::DropSequence: return "drop_sequence";
        case SchemaCapabilityOperation::Unsupported: return "unsupported";
    }
    return "unsupported";
}
