#include "core/schema_plan.h"

#include <cassert>
#include <iostream>

int main() {
    SchemaDocumentDiff diff;

    SchemaDiffOperation addTable;
    addTable.kind = SchemaDiffOperationKind::TableAdded;
    addTable.objectType = "table";
    addTable.objectPath = "/entities/products";
    addTable.objectName = "products";
    addTable.message = "Table added";
    diff.operations.push_back(addTable);

    SchemaDiffOperation dropColumn;
    dropColumn.kind = SchemaDiffOperationKind::ColumnOnlyInCurrent;
    dropColumn.objectType = "column";
    dropColumn.objectPath = "/entities/products/fields/old_price";
    dropColumn.objectName = "old_price";
    dropColumn.message = "Column exists only in current schema";
    diff.operations.push_back(dropColumn);

    const auto plan = SchemaPlanner::buildPlan(diff, DriverCapabilities::sqlite());

    assert(!plan.planId.empty());
    assert(plan.operations.size() == 2);
    assert(plan.hasExecutableOperations());
    assert(plan.hasDestructiveOperations());
    assert(plan.needsConfirmation());
    assert(plan.toSqlPreview().find("CREATE TABLE") != std::string::npos);
    assert(plan.toJsonString().find("operations") != std::string::npos);

    std::cout << "schema_plan_test passed" << std::endl;
    return 0;
}
