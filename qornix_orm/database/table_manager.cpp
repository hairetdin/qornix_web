/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "table_manager.h"
#include <sstream>
#include <stdexcept>
#include <iostream> // For logging
#include <algorithm>
#include <cctype>
#include <boost/json.hpp>

#include "exceptions.h"
#include "handler_interface.h"
#include "schema_loader.h"

namespace {
void trimInPlace(std::string &value) {
    const size_t begin = value.find_first_not_of(' ');
    if (begin == std::string::npos) {
        value.clear();
        return;
    }
    const size_t end = value.find_last_not_of(' ');
    value = value.substr(begin, end - begin + 1);
}

std::string trimCopy(std::string value) {
    trimInPlace(value);
    return value;
}

bool isAggregateExpression(const std::string &expression) {
    static const std::vector<std::string> functions = {"COUNT(", "SUM(", "AVG(", "MIN(", "MAX("};
    return std::any_of(functions.begin(), functions.end(), [&expression](const std::string &fn) {
        return expression.find(fn) != std::string::npos;
    });
}

bool hasComparisonOperatorAfter(const std::string &condition, size_t offset) {
    static const std::vector<std::string> operators = {"=", ">", "<", ">=", "<=", "!="};
    return std::any_of(operators.begin(), operators.end(), [&condition, offset](const std::string &op) {
        return condition.find(op, offset) != std::string::npos;
    });
}

bool isLooselyNumeric(const std::string &value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) || c == '.' || c == '-';
    });
}

bool isStrictNumeric(const std::string &value) {
    if (!isLooselyNumeric(value)) {
        return false;
    }
    return std::count(value.begin(), value.end(), '.') <= 1;
}

std::string normalizeFilterOperator(const std::string &op) {
    if (op == "gte") {
        return ">=";
    }
    if (op == "gt") {
        return ">";
    }
    if (op == "lte") {
        return "<=";
    }
    if (op == "lt") {
        return "<";
    }
    if (op == "ne") {
        return "!=";
    }
    if (op == "like") {
        return "LIKE";
    }
    return op;
}

std::map<std::string, std::string> parseDataString(const std::string &source) {
    std::map<std::string, std::string> data;
    std::string trimmed = trimCopy(source);
    size_t start = 0;

    while (start < trimmed.length()) {
        size_t end = trimmed.find(',', start);
        if (end == std::string::npos) {
            end = trimmed.length();
        }

        std::string pair = trimCopy(trimmed.substr(start, end - start));
        const size_t equal_pos = pair.find('=');
        if (equal_pos != std::string::npos) {
            std::string key = trimCopy(pair.substr(0, equal_pos));
            std::string value = trimCopy(pair.substr(equal_pos + 1));

            if (!value.empty() && ((value.front() == '\'' && value.back() == '\'') ||
                                   (value.front() == '"' && value.back() == '"'))) {
                value = value.substr(1, value.length() - 2);
            }
            data[key] = value;
        }

        start = end + 1;
    }

    return data;
}
} // namespace


TableManager::TableManager(const std::string &table_name, std::shared_ptr<IDatabase> db)
    : table_name_(table_name), db_(std::move(db)), limit_value_(-1) {
}

TableManager table(std::shared_ptr<IDatabase> db, const std::string &table_name) {
    return TableManager(table_name, db);
}

std::vector<std::string> TableManager::getColumnNames() {
    return db_->getColumnNames(table_name_);
}

TableManager &TableManager::limit(int limit_value) {
    limit_value_ = limit_value; // by default: -1 absence limit
    return *this;
}

TableManager &TableManager::having(const std::string &condition) {
    having_clause_ = condition;

    // Check contains whether condition table, that needs to be automatically join
    // Look for table in condition HAVING, excluding aggregate function
    size_t dot_pos = condition.find(".");
    if (dot_pos != std::string::npos && hasComparisonOperatorAfter(condition, dot_pos)) {
        // Check is whether sign equals, greater or less after dot
        size_t operator_pos = std::string::npos;
        std::vector<std::string> operators = {"=", ">", "<", ">=", "<=", "!="};
        for (const auto &op: operators) {
            operator_pos = condition.find(op, dot_pos);
            if (operator_pos != std::string::npos) {
                break;
            }
        }

        if (operator_pos != std::string::npos) {
            // Extract Name table before dot
            std::string potential_table = condition.substr(0, dot_pos);

            // Check is whether potential table aggregate function
            bool is_aggregate_function = isAggregateExpression(potential_table);

            // If this not aggregate function, Process as regular table
            if (!is_aggregate_function) {
                // Check that this not main table
                if (potential_table != table_name_) {
                    // Add table in join_tables_, if it another not added
                    if (std::find(join_tables_.begin(), join_tables_.end(), potential_table) ==
                        join_tables_.end()) {
                        join_tables_.push_back(potential_table);
                        // Create correct JOIN condition on basis names tables
                        std::string on_condition;
                        on_condition = generateJoinCondition(potential_table, table_name_);
                        join_clauses_[potential_table] = on_condition;
                    }
                }
            }
        }
    }

    return *this;
}

std::string TableManager::generateJoinCondition(const std::string &table1, const std::string &table2) {
    // First try use information from schema application
    auto table1Fks = getForeignKeysForTable(table1);
    for (const auto &fk: table1Fks) {
        if (fk.references == table2) {
            return table1 + "." + fk.name + " = " + table2 + "." + fk.toField;
        }
    }

    // Check reverse relation in table table1
    auto entities = SchemaLoader::getEntities();
    auto entityIt1 = entities.find(table1);
    if (entityIt1 != entities.end()) {
        const auto& entity = entityIt1->second;

        // Check fields on presence of reverse relations
        for (const auto& field : entity.fields) {
            if (field.isReverseRelation && field.references == table2) {
                // This reverse relation: table2 contains foreign key, referencing on table1
                // Find foreign key in table2, which references on table1
                auto table2Fks = getForeignKeysForTable(table2);
                for (const auto& fk : table2Fks) {
                    if (fk.references == table1) {
                        return table2 + "." + fk.name + " = " + table1 + "." + fk.toField;
                    }
                }
            }
        }

        // Also Check reverseRelations
        for (const auto& reverseRel : entity.reverseRelations) {
            if (reverseRel.isReverseRelation && reverseRel.references == table2) {
                // This reverse relation: table2 contains foreign key, referencing on table1
                // Find foreign key in table2, which references on table1
                auto table2Fks = getForeignKeysForTable(table2);
                for (const auto& fk : table2Fks) {
                    if (fk.references == table1) {
                        return table2 + "." + fk.name + " = " + table1 + "." + fk.toField;
                    }
                }
            }
        }
    }

    // Check reverse relation in table table2
    auto entityIt2 = entities.find(table2);
    if (entityIt2 != entities.end()) {
        const auto& entity = entityIt2->second;

        // Check fields on presence of reverse relations
        for (const auto& field : entity.fields) {
            if (field.isReverseRelation && field.references == table1) {
                // This reverse relation: table1 contains foreign key, referencing on table2
                // Find foreign key in table1, which references on table2
                auto table1Fks = getForeignKeysForTable(table1);
                for (const auto& fk : table1Fks) {
                    if (fk.references == table2) {
                        return table1 + "." + fk.name + " = " + table2 + "." + fk.toField;
                    }
                }
            }
        }

        // Also Check reverseRelations
        for (const auto& reverseRel : entity.reverseRelations) {
            if (reverseRel.isReverseRelation && reverseRel.references == table1) {
                // This reverse relation: table1 contains foreign key, referencing on table2
                // Find foreign key in table1, which references on table2
                auto table1Fks = getForeignKeysForTable(table1);
                for (const auto& fk : table1Fks) {
                    if (fk.references == table2) {
                        return table1 + "." + fk.name + " = " + table2 + "." + fk.toField;
                    }
                }
            }
        }
    }

    // Check reverse relation through getForeignKeysForTable (old logic)
    auto table2Fks = getForeignKeysForTable(table2);
    for (const auto &fk: table2Fks) {
        if (fk.references == table1) {
            return table2 + "." + fk.name + " = " + table1 + "." + fk.toField;
        }
    }

    // If not found information o foreign keys, Use primitive logic
    std::vector<std::string> table1_columns;
    std::vector<std::string> table2_columns;

    try {
        table1_columns = db_->getColumnNames(table1);
        table2_columns = db_->getColumnNames(table2);
    } catch (...) {
        // If not can get columns, Use standard convention
        return table1 + "." + table2 + "_id = " + table2 + ".id";
    }

    // Look for potential foreign keys in first table, referencing on second
    for (const auto &column: table1_columns) {
        if (column == table2 + "_id") {
            return table1 + "." + column + " = " + table2 + ".id";
        }
    }

    // Look for potential foreign keys in second table, referencing on first
    for (const auto &column: table2_columns) {
        if (column == table1 + "_id") {
            return table2 + "." + column + " = " + table1 + ".id";
        }
    }

    // By default Use standard convention
    return table1 + "." + table2 + "_id = " + table2 + ".id";
}


// Parse conditions filter
std::string TableManager::parseFilterCondition(const std::string &source_condition) {
    std::string condition = source_condition;
    trimInPlace(condition);

    if (condition.empty()) {
        return "";
    }

    if (condition.find('&') != std::string::npos || condition.find('|') != std::string::npos ||
        condition.find('(') != std::string::npos) {
        return parseComplexFilterCondition(condition);
    }

    size_t equal_pos = condition.find('=');
    if (equal_pos == std::string::npos) {
        const std::vector<std::string> operators = {">=", "<=", "!=", ">", "<", " LIKE "};
        for (const auto &op: operators) {
            size_t op_pos = condition.find(op);
            if (op_pos == std::string::npos) {
                continue;
            }

            std::string field_part = condition.substr(0, op_pos);
            std::string value_part = condition.substr(op_pos + op.length());
            trimInPlace(field_part);
            trimInPlace(value_part);

            size_t dot_pos = field_part.find(".");
            if (dot_pos == std::string::npos) {
                return formatFilterCondition(table_name_ + "." + field_part, op, value_part);
            }

            std::string table_name = field_part.substr(0, dot_pos);
            std::string field_and_op = field_part.substr(dot_pos + 1);
            size_t op_underscore_pos = field_and_op.find("__");
            if (op_underscore_pos == std::string::npos) {
                const std::string sql_op = (op == " LIKE ") ? "LIKE" : op;
                return formatFilterCondition(table_name + "." + field_and_op, sql_op, value_part);
            }

            std::string field_name = field_and_op.substr(0, op_underscore_pos);
            std::string op_type = field_and_op.substr(op_underscore_pos + 2);
            if (op_type == "in") {
                return table_name + "." + field_name + " IN (" + value_part + ")";
            }
            return formatFilterCondition(table_name + "." + field_name, normalizeFilterOperator(op_type), value_part);
        }
        return condition;
    }

    std::string field_part = condition.substr(0, equal_pos);
    std::string value = condition.substr(equal_pos + 1);
    size_t dot_pos = field_part.find(".");

    if (dot_pos != std::string::npos) {
        std::string table_name = field_part.substr(0, dot_pos);
        std::string field_and_op = field_part.substr(dot_pos + 1);
        size_t op_underscore_pos = field_and_op.find("__");
        if (op_underscore_pos == std::string::npos) {
            return formatFilterCondition(table_name + "." + field_and_op, "=", value);
        }

        std::string field_name = field_and_op.substr(0, op_underscore_pos);
        std::string op_type = field_and_op.substr(op_underscore_pos + 2);
        if (op_type == "in") {
            return table_name + "." + field_name + " IN (" + value + ")";
        }
        return formatFilterCondition(table_name + "." + field_name, normalizeFilterOperator(op_type), value);
    }

    size_t op_pos = field_part.find("__");
    if (op_pos == std::string::npos) {
        return formatFilterCondition(table_name_ + "." + field_part, "=", value);
    }

    std::string first_part = field_part.substr(0, op_pos);
    std::string second_part = field_part.substr(op_pos + 2);
    if (std::find(join_tables_.begin(), join_tables_.end(), first_part) != join_tables_.end()) {
        return formatFilterCondition(first_part + "." + second_part, "=", value);
    }

    if (second_part == "in") {
        return table_name_ + "." + first_part + " IN (" + value + ")";
    }
    return formatFilterCondition(table_name_ + "." + first_part, normalizeFilterOperator(second_part), value);
}

std::string TableManager::formatFilterCondition(const std::string &field, const std::string &op,
                                                const std::string &value) {
    // Check is whether value number
    bool is_numeric = isLooselyNumeric(value);

    if (is_numeric) {
        return field + " " + op + " " + value;
    } else {
        return field + " " + op + " '" + value + "'";
    }
}

// Method for parsing complex conditions:
std::string TableManager::parseComplexFilterCondition(const std::string &condition) {
    std::string result;
    std::string current_token;
    bool in_quotes = false;

    for (size_t i = 0; i < condition.length(); ++i) {
        char ch = condition[i];

        if (ch == '\'' || ch == '"') {
            in_quotes = !in_quotes;
            current_token += ch;
        } else if (!in_quotes && (ch == '(' || ch == ')')) {
            // Process token before parenthesis
            if (!current_token.empty()) {
                result += " " + parseFilterToken(current_token) + " ";
                current_token.clear();
            }
            result += ch == '(' ? " ( " : " ) ";
        } else if (!in_quotes && (ch == '&' || ch == '|')) {
            // Process token before operator
            if (!current_token.empty()) {
                result += " " + parseFilterToken(current_token) + " ";
                current_token.clear();
            }

            // Check is whether this double operator (&& or ||)
            if (i + 1 < condition.length() &&
                ((ch == '&' && condition[i + 1] == '&') || (ch == '|' && condition[i + 1] == '|'))) {
                result += (ch == '&') ? " AND " : " OR ";
                i++; // Skip next character
            } else {
                result += (ch == '&') ? " AND " : " OR ";
            }
        } else {
            current_token += ch;
        }
    }

    // Process last token
    if (!current_token.empty()) {
        result += " " + parseFilterToken(current_token) + " ";
    }

    return result;
}

// Add Method for parsing separate tokens:
std::string TableManager::parseFilterToken(const std::string &token) {
    // Trim leading and trailing whitespace
    std::string trimmed = token;
    trimInPlace(trimmed);

    if (trimmed.empty()) {
        return "";
    }

    // If this already SQL-condition (contains operator), Return as is
    if (trimmed.find(" = ") != std::string::npos ||
        trimmed.find(" > ") != std::string::npos ||
        trimmed.find(" < ") != std::string::npos ||
        trimmed.find(" >= ") != std::string::npos ||
        trimmed.find(" <= ") != std::string::npos ||
        trimmed.find(" != ") != std::string::npos ||
        trimmed.find(" LIKE ") != std::string::npos) {
        return trimmed;
    }

    // If this simple condition of the form field=value, Parse its
    size_t equal_pos = trimmed.find('=');
    if (equal_pos != std::string::npos) {
        std::string field_part = trimmed.substr(0, equal_pos);
        std::string value = trimmed.substr(equal_pos + 1);
        return parseSimpleFilterCondition(field_part + "=" + value);
    }

    return trimmed;
}

// Method for parsing simple conditions:
std::string TableManager::parseSimpleFilterCondition(const std::string &source_condition) {
    std::string condition = source_condition;
    trimInPlace(condition);

    if (condition.empty()) {
        return "";
    }

    size_t equal_pos = condition.find('=');
    if (equal_pos == std::string::npos) {
        return condition;
    }

    std::string field_part = condition.substr(0, equal_pos);
    std::string value = condition.substr(equal_pos + 1);
    size_t dot_pos = field_part.find(".");

    if (dot_pos != std::string::npos) {
        std::string table_name = field_part.substr(0, dot_pos);
        std::string field_and_op = field_part.substr(dot_pos + 1);
        size_t op_underscore_pos = field_and_op.find("__");
        if (op_underscore_pos == std::string::npos) {
            return formatFilterCondition(table_name + "." + field_and_op, "=", value);
        }

        std::string field_name = field_and_op.substr(0, op_underscore_pos);
        std::string op_type = field_and_op.substr(op_underscore_pos + 2);
        if (op_type == "in") {
            return table_name + "." + field_name + " IN (" + value + ")";
        }
        return formatFilterCondition(table_name + "." + field_name, normalizeFilterOperator(op_type), value);
    }

    size_t op_pos = field_part.find("__");
    if (op_pos == std::string::npos) {
        return formatFilterCondition(table_name_ + "." + field_part, "=", value);
    }

    std::string field = field_part.substr(0, op_pos);
    std::string op = field_part.substr(op_pos + 2);
    if (op == "in") {
        return field + " IN (" + value + ")";
    }
    return formatFilterCondition(field, normalizeFilterOperator(op), value);
}

TableManager &TableManager::filter(const std::string &field, const std::string &value) {
    std::string condition = field + " = '" + value + "'";
    where_conditions_.push_back(condition);
    return *this;
}

// Overload method filter for working with map:
TableManager &TableManager::filter(const std::map<std::string, std::string> &conditions) {
    for (const auto &pair: conditions) {
        std::string condition = pair.first + " = '" + pair.second + "'";
        where_conditions_.push_back(condition);
    }
    return *this;
}

// Updated Implementation existing method filter:
TableManager &TableManager::filter(const std::string &condition) {
    // Check contains whether condition dot
    size_t dot_pos = condition.find(".");
    if (dot_pos != std::string::npos) {
        // Check is whether sign equals after dot
        size_t equal_pos = condition.find('=', dot_pos);
        if (equal_pos != std::string::npos) {
            // Extract Name table before dot
            std::string potential_table = condition.substr(0, dot_pos);

            // Check that this not main table
            // if (potential_table != table_name_) {
            //     // Add table in join_tables_, if it another not added
            //     if (std::find(join_tables_.begin(), join_tables_.end(), potential_table) ==
            //         join_tables_.end()) {
            //         join_tables_.push_back(potential_table);
            //         // Create correct JOIN condition on basis names tables
            //         std::string on_condition;
            //         on_condition = generateJoinCondition(potential_table, table_name_);
            //         join_clauses_[potential_table] = on_condition;
            //     }
            // }
        }
    }

    // Check contains whether condition double underscore (for backward compatibility)
    size_t first_double_underscore = condition.find("__");
    if (first_double_underscore != std::string::npos) {
        // Check is whether sign equals after first double underscore
        size_t equal_pos = condition.find('=', first_double_underscore);
        if (equal_pos != std::string::npos) {
            // Extract possible Name table before first double underscore
            std::string potential_table = condition.substr(0, first_double_underscore);

            // Check is whether another one double underscore (for operators)
            size_t second_double_underscore = condition.find("__", first_double_underscore + 2);
            if (second_double_underscore != std::string::npos && second_double_underscore < equal_pos) {
                // This condition in format table__field__operator=value
                // Check that this not main table
                if (potential_table != table_name_) {
                    // Add table in join_tables_, if it another not added
                    if (std::find(join_tables_.begin(), join_tables_.end(), potential_table) ==
                        join_tables_.end()) {
                        join_tables_.push_back(potential_table);
                        // Create correct JOIN condition on basis names tables
                        std::string on_condition;
                        on_condition = generateJoinCondition(potential_table, table_name_);
                        join_clauses_[potential_table] = on_condition;
                    }
                }
            } else if (second_double_underscore == std::string::npos) {
                // This can be table__field=value or field__operator=value
                // Check is whether sign equals after first double underscore
                std::string after_first_underscore = condition.substr(first_double_underscore + 2);
                size_t next_equal = after_first_underscore.find('=');
                if (next_equal != std::string::npos) {
                    std::string operator_part = after_first_underscore.substr(0, next_equal);
                    // If operator_part - this standard operator, then this field main table with operator
                    if (operator_part == "gte" || operator_part == "gt" || operator_part == "lte" ||
                        operator_part == "lt" || operator_part == "ne" || operator_part == "in" ||
                        operator_part == "like") {
                        // This field main table with operator, not Add JOIN
                    } else {
                        // Treat as, that this table__field=value
                        // Check that this not main table
                        if (potential_table != table_name_) {
                            if (std::find(join_tables_.begin(), join_tables_.end(), potential_table) ==
                                join_tables_.end()) {
                                join_tables_.push_back(potential_table);
                                // Create correct JOIN condition on basis names tables
                                std::string on_condition;
                                on_condition = generateJoinCondition(potential_table, table_name_);
                                join_clauses_[potential_table] = on_condition;
                            }
                        }
                    }
                }
            }
        }
    }

    // Check is whether condition string with operators
    std::string parsed_condition = parseFilterCondition(condition);
    where_conditions_.push_back(parsed_condition);
    return *this;
}

std::map<std::string, std::string> TableManager::get(const std::map<std::string, std::string> &conditions) {
    // Create new instance TableManager for execution request
    TableManager query_manager = *this;

    // Apply filters and limit
    query_manager.filter(conditions).limit(1);

    // Execute request
    auto results = query_manager.execute();

    // If is results, Return first record
    if (!results.empty()) {
        return results[0];
    }

    // If results no, throw NotFoundError
    throw NotFoundError("Record not found matching the given conditions");
}

std::map<std::string, std::string> TableManager::get(const std::string &condition) {
    // Create new instance TableManager for execution request
    TableManager query_manager(table_name_, db_);

    // Copy existing parameters
    query_manager.where_conditions_ = where_conditions_;
    query_manager.order_clause_ = order_clause_;
    query_manager.group_clause_ = group_clause_;
    query_manager.selected_fields_ = selected_fields_;

    // Apply filter and limit
    query_manager.filter(condition).limit(1);

    // Execute request
    auto results = query_manager.execute();

    // If is results, Return first record
    if (!results.empty()) {
        return results[0];
    }

    // If results no, throw NotFoundError
    throw NotFoundError("Record not found matching the given conditions");
}

// Create by dictionary
std::map<std::string, std::string> TableManager::create(const std::map<std::string, std::string> &data) {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    if (data.empty()) {
        throw std::runtime_error("No data provided for insertion");
    }

    std::ostringstream query;

    query << "INSERT INTO " << "\"" + table_name_ + "\"" << " (";

    // Build List fields
    auto it = data.begin();
    query << it->first;
    ++it;
    while (it != data.end()) {
        query << ", " << it->first;
        ++it;
    }

    query << ") VALUES (";

    // Build List values
    it = data.begin();
    bool is_numeric = isStrictNumeric(it->second);

    if (is_numeric) {
        query << it->second;
    } else {
        query << "'" << it->second << "'";
    }

    ++it;
    while (it != data.end()) {
        is_numeric = isStrictNumeric(it->second);

        if (is_numeric) {
            query << ", " << it->second;
        } else {
            query << ", '" << it->second << "'";
        }
        ++it;
    }

    query << ") RETURNING *";

    std::cout << "Executing insert query: " << query.str() << std::endl;

    std::string raw_result = db_->executeQuery(query.str());
    auto results = parseResult(raw_result);

    if (!results.empty()) {
        return results[0];
    }

    throw std::runtime_error("Failed to insert record");
}


// Create by vector many records
std::vector<std::map<std::string, std::string> >
TableManager::create(const std::vector<std::map<std::string, std::string> > &data_list) {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    if (data_list.empty()) {
        throw std::runtime_error("No data provided for insertion");
    }

    std::vector<std::map<std::string, std::string> > inserted_records;

    for (const auto &data: data_list) {
        auto result = create(data);
        inserted_records.push_back(result);
    }

    return inserted_records;
}

// Create from rows create("name='Suny', email='suny@example.com', age=15")
std::map<std::string, std::string> TableManager::create(const std::string &data_string) {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    if (data_string.empty()) {
        throw std::runtime_error("No data provided for insertion");
    }

    std::map<std::string, std::string> data = parseDataString(data_string);
    if (data.empty()) {
        throw std::runtime_error("Failed to parse data string");
    }

    return create(data);
}

int TableManager::executeUpdate(const std::string &query) {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    std::cout << "Executing update query: " << query << std::endl;

    try {
        // Use executeUpdate for getting number of updated rows
        int updated_rows = db_->executeUpdate(query);
        return updated_rows;
    } catch (const std::exception &e) {
        throw std::runtime_error("Update query failed: " + std::string(e.what()));
    }
}

// Update records using map
int TableManager::update(const std::map<std::string, std::string> &data) {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    if (data.empty()) {
        throw std::runtime_error("No data provided for update");
    }

    if (where_conditions_.empty()) {
        throw std::runtime_error("Update operation requires WHERE conditions to prevent updating all rows");
    }

    std::ostringstream query;
    query << "UPDATE " << table_name_ << " SET ";

    // Build List fields and values for update
    auto it = data.begin();
    bool is_numeric = isStrictNumeric(it->second);

    if (is_numeric) {
        query << it->first << " = " << it->second;
    } else {
        query << it->first << " = '" << it->second << "'";
    }

    ++it;
    while (it != data.end()) {
        is_numeric = isStrictNumeric(it->second);

        if (is_numeric) {
            query << ", " << it->first << " = " << it->second;
        } else {
            query << ", " << it->first << " = '" << it->second << "'";
        }
        ++it;
    }

    // Add WHERE conditions
    query << " WHERE ";
    for (size_t i = 0; i < where_conditions_.size(); ++i) {
        if (i > 0) query << " AND ";
        query << where_conditions_[i];
    }

    return executeUpdate(query.str());
}


// Update records using rows in format "name='New Name', email='new@example.com'"
int TableManager::update(const std::string &data_string) {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    if (data_string.empty()) {
        throw std::runtime_error("No data provided for update");
    }

    if (where_conditions_.empty()) {
        throw std::runtime_error("Update operation requires WHERE conditions to prevent updating all rows");
    }

    std::map<std::string, std::string> data = parseDataString(data_string);
    if (data.empty()) {
        throw std::runtime_error("Failed to parse data string");
    }

    return update(data);
}

int TableManager::delete_() {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    if (where_conditions_.empty()) {
        throw std::runtime_error("Delete operation requires WHERE conditions to prevent deleting all rows");
    }

    std::ostringstream query;
    query << "DELETE FROM " << table_name_;

    // Add WHERE conditions
    query << " WHERE ";
    for (size_t i = 0; i < where_conditions_.size(); ++i) {
        if (i > 0) query << " AND ";
        query << where_conditions_[i];
    }

    return executeUpdate(query.str());
}

int TableManager::remove() {
    return delete_();
}

TableManager &TableManager::order_by(const std::string &ordering) {
    if (!ordering.empty()) {
        if (ordering[0] == '-') {
            // Handle DESC: remove minus and Add DESC
            std::string field_name = ordering.substr(1);
            order_clause_ = field_name + " DESC";
        } else {
            // Handle ASC: Add ASC if not specified direction
            if (ordering.find(" ASC", ordering.length() - 4) != std::string::npos ||
                ordering.find(" DESC", ordering.length() - 5) != std::string::npos) {
                order_clause_ = ordering; // Already contains direction
                } else {
                    order_clause_ = ordering + " ASC";
                }
        }
    }
    return *this;
}

TableManager &TableManager::group_by(const std::string &grouping) {
    group_clause_ = grouping;
    return *this;
}

TableManager &TableManager::join(const std::string &table, const std::string &on_condition) {
    // Check exists whether already such table in join_tables_
    if (std::find(join_tables_.begin(), join_tables_.end(), table) == join_tables_.end()) {
        join_tables_.push_back(table);
        join_clauses_[table] = on_condition;
    }
    return *this;
}

TableManager &TableManager::values(const std::vector<std::string> &fields) {
    selected_fields_ = fields;

    // Process fields with dot (related table)
    for (const auto &field: fields) {
        // Check contains whether field alias (as)
        size_t as_pos = field.find(" as ");
        if (as_pos != std::string::npos) {
            // Extract part before " as "
            std::string field_part = field.substr(0, as_pos);

            // Check is whether field aggregate function
            bool is_aggregate_function = isAggregateExpression(field_part);

            // If this aggregate function, Check presence tables inside it
            if (is_aggregate_function) {
                // Look for table inside aggregate function
                // For example, in "AVG(product.price)" look for "product"
                size_t open_paren_pos = field_part.find('(');
                size_t close_paren_pos = field_part.find(')');

                if (open_paren_pos != std::string::npos && close_paren_pos != std::string::npos &&
                    close_paren_pos > open_paren_pos) {
                    std::string content_inside = field_part.substr(open_paren_pos + 1,
                                                                   close_paren_pos - open_paren_pos - 1);

                    // Check contains whether contents dot
                    size_t dot_pos = content_inside.find(".");
                    if (dot_pos != std::string::npos) {
                        // Use processNestedPath for handling nested path
                        std::string resolved_table, field_name;
                        processNestedPath(content_inside, resolved_table, field_name);
                    }
                }
                continue;
            }

            // Check contains whether field_part dot
            size_t dot_pos = field.find(".");
            if (dot_pos != std::string::npos) {
                // Use processNestedPath for handling nested path
                std::string resolved_table, field_name;
                processNestedPath(field, resolved_table, field_name);
            }
        } else {
            // Check is whether field aggregate function
            bool is_aggregate_function = isAggregateExpression(field);

            // If this aggregate function, Check presence tables inside it
            if (is_aggregate_function) {
                // Look for table inside aggregate function
                size_t open_paren_pos = field.find('(');
                size_t close_paren_pos = field.find(')');

                if (open_paren_pos != std::string::npos && close_paren_pos != std::string::npos &&
                    close_paren_pos > open_paren_pos) {
                    std::string content_inside = field.substr(open_paren_pos + 1,
                                                              close_paren_pos - open_paren_pos - 1);

                    // Check contains whether contents dot
                    size_t dot_pos = content_inside.find(".");
                    if (dot_pos != std::string::npos) {
                        // Use processNestedPath for handling nested path
                        std::string resolved_table, field_name;
                        processNestedPath(content_inside, resolved_table, field_name);
                    }
                }
                continue;
            }

            size_t dot_pos = field.find(".");
            if (dot_pos != std::string::npos) {
                // Use processNestedPath for handling nested path
                std::string resolved_table, field_name;
                processNestedPath(field, resolved_table, field_name);
            }
        }
    }

    return *this;
}

std::vector<std::map<std::string, std::string> > TableManager::all() {
    // Reset conditions filtering and sorting
    //                where_conditions_.clear();
    //                order_clause_.clear();

    // Execute request
    return execute();
}

std::string TableManager::buildSelectQuery() {
    std::ostringstream query;

    // Use selected fields, if they specified, Otherwise all fields (*)
    if (!selected_fields_.empty()) {
        query << "SELECT ";
        for (size_t i = 0; i < selected_fields_.size(); ++i) {
            if (i > 0) query << ", ";
            std::string current_table = table_name_;
            std::string field_name_full = selected_fields_[i];

            // Check is whether field aggregate function
            bool is_aggregate_function = isAggregateExpression(selected_fields_[i]);

            if (is_aggregate_function) {
                // Check presence alias (as)
                size_t as_pos = selected_fields_[i].find(" as ");
                if (as_pos != std::string::npos) {
                    // If is alias, Use field as is (including alias)
                    query << selected_fields_[i];
                } else {
                    // If alias not specified, Add aggregate function and Create alias
                    query << selected_fields_[i];
                    // Extract Name function for alias
                    size_t open_paren_pos = selected_fields_[i].find('(');
                    if (open_paren_pos != std::string::npos) {
                        std::string func_name = selected_fields_[i].substr(0, open_paren_pos);
                        // Convert to lower case for alias
                        std::transform(func_name.begin(), func_name.end(), func_name.begin(), ::tolower);
                        query << " AS \"" << func_name << "\"";
                    }
                }
            } else {
                // Check contains whether field alias (as)
                size_t as_pos = selected_fields_[i].find(" as ");
                if (as_pos != std::string::npos) {
                    // Extract part before " as " and after
                    std::string field_without_alias = selected_fields_[i].substr(0, as_pos);
                    std::string alias_part = selected_fields_[i].substr(as_pos + 4); // " as " = 4 characters

                    // Check contains whether field_without_alias dot
                    size_t dot_pos = field_without_alias.find(".");
                    if (dot_pos != std::string::npos) {
                        // This nested path - Use Helper Method
                        std::string resolved_table, field_name;
                        processNestedPath(field_without_alias, resolved_table, field_name);
                        query << resolved_table << "." << field_name << " AS \"" << alias_part << "\"";
                    } else {
                        // This format field as alias
                        query << table_name_ << "." << field_name_full << " AS \"" << alias_part << "\"";
                    }
                } else {
                    // Process fields with dot (related table)
                    size_t dot_pos = field_name_full.find(".");
                    if (dot_pos != std::string::npos) {
                        // This nested path - Use Helper Method
                        std::string resolved_table, field_name;
                        processNestedPath(field_name_full, resolved_table, field_name);
                        query << resolved_table << "." << field_name;
                        // Add alias for fields from related tables
                        query << " AS \"" << resolved_table << "." << field_name << "\"";
                    } else {
                        // This format field as alias
                        query << table_name_ << "." << field_name_full;
                    }
                }
            }
        }
    } else {
        // For SELECT * Get names columns and Create aliases for each
        std::vector<std::string> column_names = getColumnNames();
        if (!column_names.empty()) {
            query << "SELECT ";
            for (size_t i = 0; i < column_names.size(); ++i) {
                if (i > 0) query << ", ";
                query << table_name_ << "." << column_names[i];
                query << " AS \"" << column_names[i] << "\"";
            }
        } else {
            query << "SELECT *";
        }
    }

    query << " FROM " << table_name_;

    // Add JOIN clauses
    for (const auto &table: join_tables_) {
        if (join_clauses_.find(table) != join_clauses_.end()) {
            // Check is whether this obratnoy relation
            auto reverseRelations = getReverseRelationFields(table_name_);
            bool isReverseJoin = std::find(reverseRelations.begin(),
                                         reverseRelations.end(),
                                         table) != reverseRelations.end();

            if (isReverseJoin) {
                query << " LEFT JOIN " << table << " ON " << join_clauses_[table];
            } else {
                query << " JOIN " << table << " ON " << join_clauses_[table];
            }
        }
    }

    if (!where_conditions_.empty()) {
        query << " WHERE ";
        for (size_t i = 0; i < where_conditions_.size(); ++i) {
            if (i > 0) query << " AND ";
            query << where_conditions_[i];
        }
    }

    if (!group_clause_.empty()) {
        query << " GROUP BY " << group_clause_;
    }

    if (!having_clause_.empty()) {
        query << " HAVING " << having_clause_;
    }

    if (!order_clause_.empty()) {
        query << " ORDER BY " << order_clause_;
    }

    // if value set (not equals -1)
    if (limit_value_ >= 0) {
        query << " LIMIT " << limit_value_;
    }

    return query.str();
}

std::vector<std::map<std::string, std::string> > TableManager::parseResult(const std::string &raw_result) {
    std::vector<std::map<std::string, std::string> > result;

    // Parse result
    std::istringstream result_stream(raw_result);
    std::string line;

    while (std::getline(result_stream, line)) {
        std::map<std::string, std::string> row;
        std::istringstream line_stream(line);
        std::string field;
        size_t field_index = 0;

        while (std::getline(line_stream, field, '\t')) {
            if (!selected_fields_.empty()) {
                // If at we is information o selected fields, Use ikh directly
                if (field_index < selected_fields_.size()) {
                    // Check is whether field aggregate function
                    bool is_aggregate_function = isAggregateExpression(selected_fields_[field_index]);

                    if (is_aggregate_function) {
                        // Check presence alias (as)
                        size_t as_pos = selected_fields_[field_index].find(" as ");
                        if (as_pos != std::string::npos) {
                            // Extract alias
                            std::string alias_part = selected_fields_[field_index].substr(as_pos + 4);
                            row[alias_part] = field;
                        } else {
                            // Extract Name function for alias
                            size_t open_paren_pos = selected_fields_[field_index].find('(');
                            if (open_paren_pos != std::string::npos) {
                                std::string func_name = selected_fields_[field_index].substr(0,
                                    open_paren_pos);
                                // Convert to lower case
                                std::transform(func_name.begin(), func_name.end(), func_name.begin(),
                                               ::tolower);
                                row[func_name] = field;
                            } else {
                                // If not can determine Name function, Use index
                                row["column" + std::to_string(field_index)] = field;
                            }
                        }
                    } else {
                        // Check contains whether field alias (as)
                        size_t as_pos = selected_fields_[field_index].find(" as ");
                        if (as_pos != std::string::npos) {
                            // Extract alias
                            std::string alias_part = selected_fields_[field_index].substr(as_pos + 4);
                            row[alias_part] = field;
                        } else {
                            // Check contains whether field dot
                            size_t dot_pos = selected_fields_[field_index].find(".");
                            if (dot_pos != std::string::npos) {
                                // This format table.field
                                std::string table_name = selected_fields_[field_index].substr(0, dot_pos);
                                std::string field_name = selected_fields_[field_index].substr(dot_pos + 1);
                                row[table_name + "." + field_name] = field;
                            } else {
                                // Process fields with double underscore (for backward compatibility)
                                size_t double_underscore_pos = selected_fields_[field_index].find("__");
                                if (double_underscore_pos != std::string::npos) {
                                    std::string table_name = selected_fields_[field_index].substr(0,
                                        double_underscore_pos);
                                    std::string field_name = selected_fields_[field_index].substr(
                                        double_underscore_pos + 2);
                                    row[table_name + "__" + field_name] = field;
                                } else {
                                    // Simple field
                                    row[selected_fields_[field_index]] = field;
                                }
                            }
                        }
                    }
                }
            } else {
                // For SELECT * with aliases Use names columns from database
                std::vector<std::string> column_names = getColumnNames();
                if (field_index < column_names.size()) {
                    row[column_names[field_index]] = field;
                } else {
                    std::string column_name = "column" + std::to_string(field_index);
                    row[column_name] = field;
                }
            }
            field_index++;
        }

        if (!row.empty()) {
            result.push_back(row);
        }
    }

    return result;
}

std::vector<std::map<std::string, std::string> > TableManager::raw_sql(const std::string &sql_query) {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    std::cout << "Executing raw SQL query: " << sql_query << std::endl; // Logging request

    std::string raw_result = db_->executeQuery(sql_query);
    std::cout << "Raw result: " << raw_result << std::endl; // Logging result

    // Use common Method for parsing result
    return parseResult(raw_result);
}

std::vector<std::map<std::string, std::string> > TableManager::execute() {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    std::string query = buildSelectQuery();
    std::cout << "Executing query: " << query << std::endl; // Logging request

    std::string raw_result = db_->executeQuery(query);
    std::cout << "Raw result: " << raw_result << std::endl; // Logging result

    // Use common Method for parsing result
    return parseResult(raw_result);
}

int TableManager::count() {
    if (!db_ || !db_->isConnected()) {
        throw std::runtime_error("Database not connected");
    }

    std::string select_query = buildSelectQuery();

    // Replace SELECT... on SELECT COUNT(*)
    // Find position FROM in request
    size_t from_pos = select_query.find(" FROM ");
    if (from_pos != std::string::npos) {
        // Create new request with COUNT(*)
        std::string count_query = "SELECT COUNT(*)" + select_query.substr(from_pos);

        std::string raw_result = db_->executeQuery(count_query);

        // Parse result COUNT request
        if (!raw_result.empty()) {
            return std::stoi(raw_result);
        }
    }

    return 0;
}

std::string TableManager::get_sql() const {
    // Create copy object so as not to not modify current
    TableManager temp_manager = *this;
    return temp_manager.buildSelectQuery();
}

std::string TableManager::to_json(bool as_array) {
    auto results = execute();

    if (results.empty()) {
        return as_array ? "[]" : "{}";
    }

    if (!as_array) {
        // Only first item
        boost::json::object json_object;
        for (const auto& pair : results[0]) {
            json_object[pair.first] = pair.second;
        }
        return boost::json::serialize(json_object);
    } else {
        // All items as array
        boost::json::array json_array;
        for (const auto& row : results) {
            boost::json::object json_object;
            for (const auto& pair : row) {
                json_object[pair.first] = pair.second;
            }
            json_array.push_back(json_object);
        }
        return boost::json::serialize(json_array);
    }
}

std::vector<ForeignKeyInfo> TableManager::getForeignKeysForTable(const std::string &tableName) {
    // Get information o foreign keys from schema application
    auto entities = SchemaLoader::getEntities();
    auto entityIt = entities.find(tableName);

    if (entityIt != entities.end()) {
        std::vector<ForeignKeyInfo> result;
        for (const auto &fk: entityIt->second.foreignKeys) {
            result.push_back({fk.name, fk.references, fk.toField});
        }
        return result;
    }

    return {};
}

void TableManager::processNestedPath(const std::string& path, std::string& resolvedTable,
                                   std::string& fieldName) {
    auto entities = SchemaLoader::getEntities();

    // Split path on components
    std::vector<std::string> pathParts;
    std::string remaining = path;
    while (true) {
        size_t pos = remaining.find('.');
        if (pos == std::string::npos) {
            fieldName = remaining;
            break;
        }
        pathParts.push_back(remaining.substr(0, pos));
        remaining = remaining.substr(pos + 1);
    }
    std::string path_table_name = pathParts[pathParts.size() - 1];

    // Extract Name table from path_table_name, considering suffix __set
    std::string actual_table_name = extractTableNameFromReverseRelation(path_table_name);

    resolvedTable = actual_table_name;
    // for (const auto& join_table  : join_tables_) {
    //     std::cout  << "DEBUG: join_table: " << join_table << std::endl;
    // }

    // for (const auto& join_clause: join_clauses_) {
    //     std::cout << "DEBUG: join_clause: " << join_clause.first << " " << join_clause.second << std::endl;
    // }

    if (std::find(join_tables_.begin(), join_tables_.end(), actual_table_name) != join_tables_.end()) {
        // if explicitly specified join, then take name table from path, this explicitly specified table in JOIN
        std::cout << "DEBUG: resolvedTable from join path: " << resolvedTable << std::endl;
        return;
    }

    // Additional check: table can be in join_clauses_ as result reverse relation
    for (const auto& clause : join_clauses_) {
        // Check is contained whether actual_table_name in string conditions JOIN
        // Consider both format: "actual_table_name.field = other_table.other_field"
        // or "other_table.other_field = actual_table_name.field"
        if (clause.second.find(actual_table_name + ".") != std::string::npos) {
            resolvedTable = actual_table_name;
            // std::cout << "DEBUG: resolvedTable from join clauses: " << resolvedTable << std::endl;
            return;
        }
    }

    // If table not found in existing JOIN, continue resolution through relations
    resolvedTable = table_name_;
    // std::cout << "DEBUG: path: " << path << std::endl;

    // Iterate by all components path, krome last (kotoraya - field)
    for (const auto& relationName : pathParts) {
        // for (const auto& entity: entities) {
        //     std::cout << "DEBUG: entity " << entity.second.tableName << std::endl;
        // }
        std::string actualRelationName = relationName;
        if (relationName.find("__set") != std::string::npos) {
            actualRelationName = extractTableNameFromReverseRelation(relationName);
        }

        // Find entity by tekuschemu name table
        auto entityIt = std::find_if(entities.begin(), entities.end(),
            [&resolvedTable](const auto& pair) {

                return pair.second.tableName == resolvedTable;
            });
        // std::cout << "DEBUG: relationName: " << relationName << std::endl;
        // std::cout << "DEBUG: resolvedTable: " << resolvedTable << std::endl;

        if (entityIt == entities.end()) {
            // std::cout << "DEBUG: entity not found" << std::endl;
            break; // Not found entity
        }

        // std::cout << "DEBUG: entityIt: " << entityIt-> second.tableName << std::endl;

        const auto& entity = entityIt->second;

        // Check is whether relationName obratnoy relation
        bool isReverseRelation = false;
        std::string targetTable;

        // Check fields on presence of reverse relations
        for (const auto& field : entity.fields) {
            // std::cout << "DEBUG: field.name: " << field.name << std::endl;
            if (field.name == relationName && field.isReverseRelation) {
                targetTable = field.references;
                isReverseRelation = true;
                break;
            }
        }

        // Also Check reverseRelations
        if (!isReverseRelation) {
            for (const auto& reverseRel : entity.reverseRelations) {
                // std::cout << "DEBUG: reverseRel.name: " << reverseRel.name << std::endl;
                // std::cout << "DEBUG: reverseRel.references: " << reverseRel.references << std::endl;
                if ((reverseRel.name == relationName || reverseRel.references == actualRelationName) && reverseRel.isReverseRelation) {
                    targetTable = reverseRel.references;
                    isReverseRelation = true;
                    break;
                }
            }
        }
        // std::cout << "DEBUG: isReverseRelation: " << isReverseRelation << std::endl;

        if (isReverseRelation) {
            // This reverse relation - Add in join_tables_ for LEFT JOIN
            if (std::find(join_tables_.begin(), join_tables_.end(), targetTable) == join_tables_.end()) {
                join_tables_.push_back(targetTable);

                // Generate condition LEFT JOIN for reverse relation
                std::string joinCondition = generateLeftJoinCondition(resolvedTable, targetTable);
                if (!joinCondition.empty()) {
                    join_clauses_[targetTable] = joinCondition;
                }
            }

            resolvedTable = targetTable;
        } else {
            // Obychnaya pryamaya svyaz - look for foreign key
            std::string targetTable;
            for (const auto& fk : entity.foreignKeys) {
                if (fk.name == relationName + "_id" ||
                    fk.name == relationName ||
                    entityIt->first == relationName) {
                    targetTable = fk.references;
                    break;
                }
            }

            if (!targetTable.empty()) {
                // Add in join_tables_ for INNER JOIN
                if (std::find(join_tables_.begin(), join_tables_.end(), targetTable) == join_tables_.end()) {
                    join_tables_.push_back(targetTable);

                    // Generate condition JOIN for direct relations
                    std::string joinCondition = generateJoinCondition(resolvedTable, targetTable);
                    if (!joinCondition.empty()) {
                        join_clauses_[targetTable] = joinCondition;
                    }
                }

                resolvedTable = targetTable;
            } else {
                // If not found match, keep current table
                // But proverim, can be this already suschestvuyuschaya table in JOIN
                if (std::find(join_tables_.begin(), join_tables_.end(), actualRelationName) != join_tables_.end()) {
                    resolvedTable = actualRelationName;
                } else if (join_clauses_.find(actualRelationName) != join_clauses_.end()) {
                    resolvedTable = actualRelationName;
                }

                break;
            }
        }
    }
}


std::vector<std::string> TableManager::getReverseRelationFields(const std::string& tableName) {
    std::vector<std::string> reverseRelationFields;

    auto entities = SchemaLoader::getEntities();
    auto entityIt = entities.find(tableName);

    if (entityIt != entities.end()) {
        for (const auto& field : entityIt->second.fields) {
            if (field.isReverseRelation) {
                reverseRelationFields.push_back(field.name);
            }
        }

        // Also check reverseRelations
        for (const auto& reverseRel : entityIt->second.reverseRelations) {
            if (reverseRel.isReverseRelation) {
                reverseRelationFields.push_back(reverseRel.name);
            }
        }
    }

    return reverseRelationFields;
}

std::string TableManager::generateLeftJoinCondition(const std::string& mainTable,
                                                  const std::string& relatedTable) {
    auto entities = SchemaLoader::getEntities();

    // Find entity related table
    auto relatedEntityIt = entities.find(relatedTable);
    if (relatedEntityIt == entities.end()) {
        return "";
    }

    // Iskat foreign key, which references on main table
    for (const auto& fk : relatedEntityIt->second.foreignKeys) {
        if (fk.references == mainTable) {
            // Nayden foreign key, which references on mainTable
            return relatedTable + "." + fk.name + " = " + mainTable + "." + fk.toField;
        }
    }

    // Also check, can be this reverse relation through fields main table
    auto mainEntityIt = entities.find(mainTable);
    if (mainEntityIt != entities.end()) {
        // Check fields on presence of reverse relations
        for (const auto& field : mainEntityIt->second.fields) {
            if (field.isReverseRelation && field.references == relatedTable) {
                // Find foreign key in related table, which references on main
                for (const auto& fk : relatedEntityIt->second.foreignKeys) {
                    if (fk.references == mainTable) {
                        return relatedTable + "." + fk.name + " = " + mainTable + "." + fk.toField;
                    }
                }
            }
        }

        // Also Check reverseRelations
        for (const auto& reverseRel : mainEntityIt->second.reverseRelations) {
            if (reverseRel.isReverseRelation && reverseRel.references == relatedTable) {
                // Find foreign key in related table, which references on main
                for (const auto& fk : relatedEntityIt->second.foreignKeys) {
                    if (fk.references == mainTable) {
                        return relatedTable + "." + fk.name + " = " + mainTable + "." + fk.toField;
                    }
                }
            }
        }
    }

    return "";
}

// Helper Method for extraction name table from reverse relation (Name fields imeet takoy format - fk_name__table_name__set)
std::string TableManager::extractTableNameFromReverseRelation(const std::string& reverseRelationName) {
    // Check presence suffiksa __set
    if (reverseRelationName.find("__set") != std::string::npos) {
        // Look for predposlednee occurrence '__' before '__set'
        size_t setPos = reverseRelationName.find("__set");
        if (setPos != std::string::npos) {
            // Look for '__' before '__set'
            std::string prefix = reverseRelationName.substr(0, setPos);
            size_t pos = prefix.rfind("__");
            if (pos != std::string::npos) {
                // Extract Name table: everything after the last '__'
                return prefix.substr(pos + 2);
            }
        }
    }
    // If not contains suffix __set, Return as is
    return reverseRelationName;
}




