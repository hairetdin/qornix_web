/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "database_snapshot.h"

#include "database_interface.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string nowUtcIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }).base(), value.end());
    return value;
}

bool truthy(const std::string& value) {
    const auto v = toLower(trim(value));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

int toInt(const std::string& value, int fallback = 0) {
    try {
        if (value.empty() || value == "NULL") {
            return fallback;
        }
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::string rowValue(
    const std::map<std::string, std::string>& row,
    const std::string& key,
    const std::string& fallback = {}
) {
    auto it = row.find(key);
    if (it == row.end() || it->second == "NULL") {
        return fallback;
    }
    return it->second;
}

std::string quoteSqliteIdentifier(const std::string& identifier) {
    std::string quoted;
    quoted.reserve(identifier.size() + 2);
    quoted.push_back('"');
    for (char ch : identifier) {
        if (ch == '"') {
            quoted += "\"\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
}

DatabaseSnapshotMetadata buildMetadata(DatabaseInterface& db, const std::string& fallbackDriver) {
    DatabaseSnapshotMetadata metadata;
    const auto config = db.getDatabaseConfig();
    metadata.driverName = config.driver.empty() ? fallbackDriver : config.driver;
    metadata.databaseName = config.dbname;
    metadata.schemaName = "public";
    metadata.connectionName = config.connectionString.empty() ? config.dbname : config.connectionString;
    metadata.capturedAt = nowUtcIso8601();
    return metadata;
}

void appendError(
    DatabaseSnapshotResult& result,
    const std::string& code,
    const std::string& message,
    const std::string& context = {}
) {
    DatabaseSnapshotError error;
    error.code = code;
    error.message = message;
    error.context = context;
    result.errors.push_back(std::move(error));
}

void sortSnapshot(DatabaseSnapshot& snapshot) {
    auto byTableName = [](const auto& a, const auto& b) {
        if (a.tableName == b.tableName) {
            return a.name < b.name;
        }
        return a.tableName < b.tableName;
    };

    std::sort(snapshot.tables.begin(), snapshot.tables.end(), [](const DbTable& a, const DbTable& b) {
        return a.name < b.name;
    });

    std::sort(snapshot.columns.begin(), snapshot.columns.end(), [](const DbColumn& a, const DbColumn& b) {
        if (a.tableName == b.tableName) {
            return a.ordinalPosition < b.ordinalPosition;
        }
        return a.tableName < b.tableName;
    });

    std::sort(snapshot.primaryKeys.begin(), snapshot.primaryKeys.end(), byTableName);
    std::sort(snapshot.foreignKeys.begin(), snapshot.foreignKeys.end(), [](const DbForeignKey& a, const DbForeignKey& b) {
        if (a.tableName == b.tableName) {
            if (a.name == b.name) {
                return a.ordinalPosition < b.ordinalPosition;
            }
            return a.name < b.name;
        }
        return a.tableName < b.tableName;
    });
    std::sort(snapshot.indexes.begin(), snapshot.indexes.end(), byTableName);
    std::sort(snapshot.constraints.begin(), snapshot.constraints.end(), byTableName);
    std::sort(snapshot.views.begin(), snapshot.views.end(), [](const DbView& a, const DbView& b) {
        return a.name < b.name;
    });
    std::sort(snapshot.triggers.begin(), snapshot.triggers.end(), [](const DbTrigger& a, const DbTrigger& b) {
        if (a.tableName == b.tableName) {
            return a.name < b.name;
        }
        return a.tableName < b.tableName;
    });
}

std::string firstWordAfter(const std::string& sql, const std::string& marker) {
    auto lowerSql = toLower(sql);
    auto lowerMarker = toLower(marker);
    auto pos = lowerSql.find(lowerMarker);
    if (pos == std::string::npos) {
        return {};
    }
    pos += lowerMarker.size();
    while (pos < sql.size() && std::isspace(static_cast<unsigned char>(sql[pos]))) {
        ++pos;
    }
    std::string word;
    while (pos < sql.size() && std::isalpha(static_cast<unsigned char>(sql[pos]))) {
        word.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(sql[pos]))));
        ++pos;
    }
    return word;
}

} // namespace

bool DatabaseSnapshot::empty() const {
    return tables.empty() && views.empty() && triggers.empty();
}

bool DatabaseSnapshot::hasTable(const std::string& tableName) const {
    return std::any_of(tables.begin(), tables.end(), [&](const DbTable& table) {
        return table.name == tableName;
    });
}

std::vector<DbColumn> DatabaseSnapshot::columnsForTable(const std::string& tableName) const {
    std::vector<DbColumn> result;
    for (const auto& column : columns) {
        if (column.tableName == tableName) {
            result.push_back(column);
        }
    }
    return result;
}

std::vector<DbForeignKey> DatabaseSnapshot::foreignKeysForTable(const std::string& tableName) const {
    std::vector<DbForeignKey> result;
    for (const auto& foreignKey : foreignKeys) {
        if (foreignKey.tableName == tableName) {
            result.push_back(foreignKey);
        }
    }
    return result;
}

std::vector<DbIndex> DatabaseSnapshot::indexesForTable(const std::string& tableName) const {
    std::vector<DbIndex> result;
    for (const auto& index : indexes) {
        if (index.tableName == tableName) {
            result.push_back(index);
        }
    }
    return result;
}

std::string DatabaseSnapshot::toDebugString() const {
    std::ostringstream out;
    out << "DatabaseSnapshot(driver=" << metadata.driverName
        << ", database=" << metadata.databaseName
        << ", schema=" << metadata.schemaName
        << ", capturedAt=" << metadata.capturedAt << ")\n";

    out << "tables: " << tables.size() << "\n";
    for (const auto& table : tables) {
        out << "  table " << table.name << "\n";
        for (const auto& column : columnsForTable(table.name)) {
            out << "    column " << column.name << " " << column.type
                << " nullable=" << (column.nullable ? "true" : "false")
                << " pk=" << (column.primaryKey ? "true" : "false") << "\n";
        }
    }

    out << "views: " << views.size() << "\n";
    for (const auto& view : views) {
        out << "  view " << view.name << "\n";
    }

    out << "triggers: " << triggers.size() << "\n";
    for (const auto& trigger : triggers) {
        out << "  trigger " << trigger.name << " on " << trigger.tableName << "\n";
    }

    return out.str();
}

DatabaseSnapshotResult DatabaseIntrospector::introspect(DatabaseInterface& db) {
    const auto config = db.getDatabaseConfig();
    const auto driver = toLower(config.driver);

    if (driver == "sqlite" || driver == "sqlite3" || driver.empty()) {
        return introspectSqlite(db);
    }

    return introspectGeneric(db, driver.empty() ? "unknown" : driver);
}

DatabaseSnapshotResult DatabaseIntrospector::introspectSqlite(DatabaseInterface& db) {
    DatabaseSnapshotResult result;
    result.snapshot.metadata = buildMetadata(db, "sqlite");
    result.snapshot.metadata.driverName = "sqlite";

    try {
        const auto tablesResponse = db.exec(
            "SELECT name, sql FROM sqlite_master "
            "WHERE type='table' AND name NOT LIKE 'sqlite_%' "
            "ORDER BY name"
        );

        for (const auto& row : tablesResponse.data) {
            DbTable table;
            table.name = rowValue(row, "name");
            table.rawSql = rowValue(row, "sql");
            table.type = "table";
            if (!table.name.empty()) {
                result.snapshot.tables.push_back(table);
            }
        }

        for (const auto& table : result.snapshot.tables) {
            const auto tableNameQuoted = quoteSqliteIdentifier(table.name);

            const auto columnResponse = db.exec("PRAGMA table_info(" + tableNameQuoted + ")");
            std::vector<std::pair<int, std::string>> pkColumns;

            for (const auto& row : columnResponse.data) {
                DbColumn column;
                column.tableName = table.name;
                column.ordinalPosition = toInt(rowValue(row, "cid"));
                column.name = rowValue(row, "name");
                column.type = rowValue(row, "type");
                column.nullable = !truthy(rowValue(row, "notnull"));
                column.defaultValue = rowValue(row, "dflt_value");
                const int pkOrder = toInt(rowValue(row, "pk"));
                column.primaryKey = pkOrder > 0;

                if (column.primaryKey) {
                    pkColumns.emplace_back(pkOrder, column.name);
                    const auto lowerType = toLower(column.type);
                    column.autoIncrement = lowerType.find("int") != std::string::npos && pkOrder == 1;
                }

                if (!column.name.empty()) {
                    result.snapshot.columns.push_back(column);
                }
            }

            if (!pkColumns.empty()) {
                std::sort(pkColumns.begin(), pkColumns.end());
                DbPrimaryKey primaryKey;
                primaryKey.tableName = table.name;
                primaryKey.name = "pk_" + table.name;
                for (const auto& [_, columnName] : pkColumns) {
                    primaryKey.columnNames.push_back(columnName);
                }
                result.snapshot.primaryKeys.push_back(primaryKey);
            }

            const auto fkResponse = db.exec("PRAGMA foreign_key_list(" + tableNameQuoted + ")");
            for (const auto& row : fkResponse.data) {
                DbForeignKey foreignKey;
                foreignKey.tableName = table.name;
                foreignKey.ordinalPosition = toInt(rowValue(row, "seq"));
                foreignKey.name = "fk_" + table.name + "_" + rowValue(row, "id") + "_" + rowValue(row, "seq");
                foreignKey.columnName = rowValue(row, "from");
                foreignKey.referencedTableName = rowValue(row, "table");
                foreignKey.referencedColumnName = rowValue(row, "to");
                foreignKey.onUpdate = rowValue(row, "on_update");
                foreignKey.onDelete = rowValue(row, "on_delete");
                foreignKey.matchType = rowValue(row, "match");
                if (!foreignKey.columnName.empty()) {
                    result.snapshot.foreignKeys.push_back(foreignKey);
                }
            }

            const auto indexResponse = db.exec("PRAGMA index_list(" + tableNameQuoted + ")");
            for (const auto& row : indexResponse.data) {
                const auto indexName = rowValue(row, "name");
                const auto origin = rowValue(row, "origin");

                // SQLite autoindexes represent internal PK/UNIQUE constraints. The
                // semantic exporter can recover these from constraints later, so do
                // not expose internal names as user-visible indexes.
                if (indexName.find("sqlite_autoindex_") == 0) {
                    continue;
                }

                DbIndex index;
                index.tableName = table.name;
                index.name = indexName;
                index.unique = truthy(rowValue(row, "unique"));
                index.partial = truthy(rowValue(row, "partial"));
                index.type = origin.empty() ? "index" : origin;

                const auto rawIndexSql = db.exec(
                    "SELECT sql FROM sqlite_master WHERE type='index' AND name=?",
                    {indexName}
                );
                if (!rawIndexSql.data.empty()) {
                    index.rawSql = rowValue(rawIndexSql.data.front(), "sql");
                }

                const auto indexInfoResponse = db.exec("PRAGMA index_info(" + quoteSqliteIdentifier(indexName) + ")");
                for (const auto& infoRow : indexInfoResponse.data) {
                    const auto columnName = rowValue(infoRow, "name");
                    if (!columnName.empty()) {
                        index.columnNames.push_back(columnName);
                    }
                }

                if (!index.name.empty()) {
                    result.snapshot.indexes.push_back(index);
                }
            }
        }

        const auto viewsResponse = db.exec(
            "SELECT name, sql FROM sqlite_master "
            "WHERE type='view' ORDER BY name"
        );
        for (const auto& row : viewsResponse.data) {
            DbView view;
            view.name = rowValue(row, "name");
            view.definition = rowValue(row, "sql");
            view.materialized = false;
            if (!view.name.empty()) {
                result.snapshot.views.push_back(view);
            }
        }

        const auto triggersResponse = db.exec(
            "SELECT name, tbl_name, sql FROM sqlite_master "
            "WHERE type='trigger' ORDER BY name"
        );
        for (const auto& row : triggersResponse.data) {
            DbTrigger trigger;
            trigger.name = rowValue(row, "name");
            trigger.tableName = rowValue(row, "tbl_name");
            trigger.definition = rowValue(row, "sql");
            trigger.timing = firstWordAfter(trigger.definition, "CREATE TRIGGER " + trigger.name);
            if (trigger.timing.empty()) {
                trigger.timing = firstWordAfter(trigger.definition, "CREATE TRIGGER");
            }
            trigger.event = firstWordAfter(trigger.definition, trigger.timing);
            trigger.enabled = true;
            if (!trigger.name.empty()) {
                result.snapshot.triggers.push_back(trigger);
            }
        }

        sortSnapshot(result.snapshot);
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        appendError(result, "QORNIX_DB_SNAPSHOT_SQLITE_ERROR", e.what(), "sqlite introspection");
    }

    return result;
}

DatabaseSnapshotResult DatabaseIntrospector::introspectGeneric(
    DatabaseInterface& db,
    const std::string& driverName
) {
    DatabaseSnapshotResult result;
    result.snapshot.metadata = buildMetadata(db, driverName);

    try {
        const auto tableNames = db.getTableNames();
        for (const auto& tableName : tableNames) {
            DbTable table;
            table.name = tableName;
            table.type = "table";
            result.snapshot.tables.push_back(table);

            const auto columnNames = db.getColumnNames(tableName);
            int ordinal = 0;
            for (const auto& columnName : columnNames) {
                DbColumn column;
                column.tableName = tableName;
                column.name = columnName;
                column.type = "unknown";
                column.nullable = true;
                column.ordinalPosition = ordinal++;
                result.snapshot.columns.push_back(column);
            }
        }

        result.warnings.push_back(
            "Driver-specific typed introspection is not implemented for driver '" +
            driverName +
            "'. Generic fallback captured table and column names only."
        );
        sortSnapshot(result.snapshot);
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        appendError(result, "QORNIX_DB_SNAPSHOT_GENERIC_ERROR", e.what(), driverName);
    }

    return result;
}
