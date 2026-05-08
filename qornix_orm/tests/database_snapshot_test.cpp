/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "database_snapshot.h"
#include "database_interface.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

const DbColumn* findColumn(
    const DatabaseSnapshot& snapshot,
    const std::string& tableName,
    const std::string& columnName
) {
    for (const auto& column : snapshot.columns) {
        if (column.tableName == tableName && column.name == columnName) {
            return &column;
        }
    }
    return nullptr;
}

bool hasIndex(
    const DatabaseSnapshot& snapshot,
    const std::string& tableName,
    const std::string& indexName
) {
    for (const auto& index : snapshot.indexes) {
        if (index.tableName == tableName && index.name == indexName) {
            return true;
        }
    }
    return false;
}

bool hasForeignKey(
    const DatabaseSnapshot& snapshot,
    const std::string& tableName,
    const std::string& columnName,
    const std::string& referencedTable
) {
    for (const auto& foreignKey : snapshot.foreignKeys) {
        if (foreignKey.tableName == tableName &&
            foreignKey.columnName == columnName &&
            foreignKey.referencedTableName == referencedTable) {
            return true;
        }
    }
    return false;
}

bool hasView(const DatabaseSnapshot& snapshot, const std::string& viewName) {
    for (const auto& view : snapshot.views) {
        if (view.name == viewName) {
            return true;
        }
    }
    return false;
}

bool hasTrigger(const DatabaseSnapshot& snapshot, const std::string& triggerName) {
    for (const auto& trigger : snapshot.triggers) {
        if (trigger.name == triggerName) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    auto db = DatabaseInterface::init(":memory:", "sqlite");

    db->executeNonQuery("PRAGMA foreign_keys = ON");
    db->executeNonQuery(
        "CREATE TABLE authors ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL UNIQUE"
        ")"
    );
    db->executeNonQuery(
        "CREATE TABLE books ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "author_id INTEGER NOT NULL, "
        "title TEXT NOT NULL, "
        "price REAL DEFAULT 0, "
        "FOREIGN KEY(author_id) REFERENCES authors(id) ON DELETE CASCADE ON UPDATE NO ACTION"
        ")"
    );
    db->executeNonQuery("CREATE INDEX idx_books_title ON books(title)");
    db->executeNonQuery("CREATE VIEW book_titles AS SELECT title FROM books");
    db->executeNonQuery(
        "CREATE TRIGGER books_ai AFTER INSERT ON books "
        "BEGIN UPDATE books SET price = price WHERE id = NEW.id; END"
    );

    const auto result = DatabaseIntrospector::introspect(*db);
    if (!result.ok()) {
        std::cerr << "Snapshot failed" << std::endl;
        for (const auto& error : result.errors) {
            std::cerr << error.code << ": " << error.message << std::endl;
        }
        return 1;
    }

    const auto& snapshot = result.snapshot;
    assert(snapshot.metadata.driverName == "sqlite");
    assert(snapshot.hasTable("authors"));
    assert(snapshot.hasTable("books"));

    const auto* idColumn = findColumn(snapshot, "books", "id");
    assert(idColumn != nullptr);
    assert(idColumn->primaryKey);

    const auto* titleColumn = findColumn(snapshot, "books", "title");
    assert(titleColumn != nullptr);
    assert(!titleColumn->nullable);

    const auto* priceColumn = findColumn(snapshot, "books", "price");
    assert(priceColumn != nullptr);
    assert(priceColumn->defaultValue == "0" || priceColumn->defaultValue == "0.0");

    assert(hasForeignKey(snapshot, "books", "author_id", "authors"));
    assert(hasIndex(snapshot, "books", "idx_books_title"));
    assert(hasView(snapshot, "book_titles"));
    assert(hasTrigger(snapshot, "books_ai"));

    const auto debug = snapshot.toDebugString();
    assert(debug.find("DatabaseSnapshot") != std::string::npos);
    assert(debug.find("books") != std::string::npos);

    std::cout << "DatabaseSnapshot SQLite introspection test passed" << std::endl;
    return 0;
}
