/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "core/schema_applier.h"

#include <cassert>
#include <iostream>

int main() {
    SchemaDocumentDiff diff;
    SchemaDiffOperation op;
    op.kind = SchemaDiffOperationKind::TableAdded;
    op.objectType = "table";
    op.objectPath = "/entities/products";
    op.objectName = "products";
    diff.operations.push_back(op);

    auto plan = SchemaPlanner::buildPlan(diff, DriverCapabilities::sqlite());
    const auto result = SchemaApplier::dryRun(plan);

    assert(result.success);
    assert(result.dryRun);
    assert(!result.operations.empty());
    assert(!result.operations.front().executed);
    assert(result.operations.front().skipped);
    assert(result.toJsonString().find("dryRun") != std::string::npos);

    std::cout << "schema_applier_test passed" << std::endl;
    return 0;
}
