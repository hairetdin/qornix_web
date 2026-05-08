/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include "dynamic_api_config.h"
#include "dynamic_api_response.h"
#include "dynamic_schema_allowlist.h"
#include "database_interface.h"

#include <boost/json.hpp>

namespace qornix_dynamic_api {

class DynamicOpenApiService {
public:
    explicit DynamicOpenApiService(DynamicApiConfig config);
    DynamicApiResponse openApiJson() const;

private:
    DynamicApiConfig config_;
};

} // namespace qornix_dynamic_api
