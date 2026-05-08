#include "core/schema_history.h"

#include <cassert>
#include <cstdio>
#include <iostream>

int main() {
    const std::string historyPath = "schema_history_test_output.jsonl";
    std::remove(historyPath.c_str());

    SchemaDocumentDiff diff;
    SchemaDiffOperation op;
    op.kind = SchemaDiffOperationKind::TableAdded;
    op.objectType = "table";
    op.objectPath = "/entities/products";
    op.objectName = "products";
    diff.operations.push_back(op);

    auto plan = SchemaPlanner::buildPlan(diff, DriverCapabilities::sqlite());
    auto applyResult = SchemaApplier::dryRun(plan);

    SchemaHistoryStore store(historyPath);
    auto append = store.append(plan, applyResult, "schema_history_test", "desired_hash", "before_hash", "after_hash");
    assert(append.success);
    assert(!append.recordId.empty());

    const auto records = store.readAll();
    assert(records.size() == 1);
    assert(records.front().planId == plan.planId);
    assert(records.front().success);
    assert(records.front().dryRun);

    std::remove(historyPath.c_str());
    std::cout << "schema_history_test passed" << std::endl;
    return 0;
}
