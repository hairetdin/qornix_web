/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <map>

#include "app_struct.h"
#include "IDatabase.h"


class TableManager {
private:
    std::string table_name_;
    std::shared_ptr<IDatabase> db_;
    std::vector<std::string> where_conditions_;
    std::string order_clause_;
    std::string group_clause_;
    std::vector<std::string> selected_fields_;
    int limit_value_;
    std::map<std::string, std::string> join_clauses_; // For storage JOIN vyrazheniy
    std::vector<std::string> join_tables_; // For storage names related tables
    std::string having_clause_;

    std::vector<std::map<std::string, std::string> > parseResult(const std::string &raw_result);

public:
    TableManager(const std::string &table_name, std::shared_ptr<IDatabase> db);

    // TableManager();

    // Main Methods
    TableManager &filter(const std::string &field, const std::string &value);
    TableManager &filter(const std::string &condition);
    TableManager &filter(const std::map<std::string, std::string> &conditions);
    TableManager &order_by(const std::string &ordering);
    TableManager &group_by(const std::string &grouping);
    TableManager &values(const std::vector<std::string> &fields);
    TableManager &limit(int limit_value);
    TableManager &join(const std::string &table, const std::string &on_condition);
    TableManager &having(const std::string &condition);

    // CRUD operations
    std::map<std::string, std::string> get(const std::map<std::string, std::string> &conditions);
    std::map<std::string, std::string> get(const std::string &condition);
    std::map<std::string, std::string> create(const std::map<std::string, std::string> &data);
    std::vector<std::map<std::string, std::string> > create(
        const std::vector<std::map<std::string, std::string> > &data_list);
    std::map<std::string, std::string> create(const std::string &data_string);
    int update(const std::map<std::string, std::string> &data);
    int update(const std::string &data_string);
    int delete_();
    int remove();

    // Execute requests
    std::string get_sql() const;
    std::vector<std::map<std::string, std::string> > execute();
    std::vector<std::map<std::string, std::string> > all();
    std::vector<std::map<std::string, std::string> > raw_sql(const std::string &sql_query);
    std::string to_json(bool as_array = true);

    int count();

private:
    std::string buildSelectQuery();
    std::vector<std::string> getColumnNames();
    int executeUpdate(const std::string &query);

    // Parse conditions
    std::string parseFilterCondition(const std::string &condition);
    std::string parseComplexFilterCondition(const std::string &condition);
    std::string parseSimpleFilterCondition(const std::string &condition);
    static std::string formatFilterCondition(const std::string &field, const std::string &op, const std::string &value);
    std::string parseFilterToken(const std::string &token);

    // Generate JOIN conditions using informatsii o schema
    std::string generateJoinCondition(const std::string &table1, const std::string &table2);

    std::string generateLeftJoinCondition(const std::string &mainTable, const std::string &relatedTable);

    static std::string extractTableNameFromReverseRelation(const std::string &reverseRelationName);

    // Vspomogatelnye Methods for working with informatsiey o schema
    void processNestedPath(const std::string &path, std::string &resolvedTable, std::string &fieldName);
    std::vector<std::string> getReverseRelationFields(const std::string &tableName) ;
    std::vector<ForeignKeyInfo> getForeignKeysForTable(const std::string& tableName) ;

};

TableManager table(std::shared_ptr<IDatabase> db, const std::string &table_name);
