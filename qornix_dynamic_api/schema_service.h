/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include "dynamic_api_config.h"
#include "dynamic_api_response.h"
#include "database_interface.h"
#include "database_schema_exporter.h"
#include "database_snapshot.h"
#include "driver_capabilities.h"
#include "schema_applier.h"
#include "schema_diff_engine.h"
#include "schema_document.h"
#include "schema_history.h"
#include "schema_normalizer.h"
#include "schema_plan.h"
#include "schema_risk_policy.h"
#include "xml_schema_validator.h"

#include <boost/json.hpp>
#include <memory>
#include <string>

namespace qornix_dynamic_api {

struct SchemaPlanRequest {
    std::string xml;
    bool includeUnsupportedOperations = true;
    bool includeManualReviewOperations = true;
    bool includeSqlPreview = true;
};

struct SchemaApplyRequest {
    std::string xml;
    std::string planId;
    bool destructiveConfirmed = false;
    bool manualReviewConfirmed = false;
    bool dryRun = false;
    std::string appliedBy = "api";
};

class SchemaService {
public:
    explicit SchemaService(DynamicApiConfig config);

    DynamicApiResponse exportSchemaXml() const;
    DynamicApiResponse validateXml(const std::string& xml) const;
    DynamicApiResponse diffXml(const std::string& xml) const;
    DynamicApiResponse planXml(const SchemaPlanRequest& request) const;
    DynamicApiResponse applyPlan(const SchemaApplyRequest& request) const;
    DynamicApiResponse history() const;

    std::unique_ptr<SchemaDocument> currentSchemaDocument() const;

private:
    DynamicApiConfig config_;

    std::unique_ptr<DatabaseInterface> openDatabase() const;
    DriverCapabilities capabilitiesFor(DatabaseInterface& database) const;
    DynamicApiResponse parseDesired(const std::string& xml, std::unique_ptr<SchemaDocument>& desired) const;
    DynamicApiResponse buildDiffAndPlan(
        const std::string& xml,
        SchemaDocumentDiff& diff,
        SchemaPlan& plan
    ) const;

    static boost::json::array validationErrorsToJson(const XmlValidationResult& result);
    static boost::json::array parseErrorsToJson(const SchemaDocumentParseResult& result);
    static boost::json::value parseJsonStringOrString(const std::string& jsonText);
};

} // namespace qornix_dynamic_api
