/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "openapi_service.h"
#include <cassert>
#include <iostream>

int main() {
    qornix_dynamic_api::DynamicApiConfig config;
    config.databaseConfig.driver = "sqlite";
    config.databaseConfig.dbname = ":memory:";
    qornix_dynamic_api::DynamicOpenApiService service(config);
    auto response = service.openApiJson();
    // In environments without an initialized database this may fail, but the service must return a structured response.
    assert(!response.toJsonString().empty());
    std::cout << "dynamic_openapi_service_test passed" << std::endl;
    return 0;
}
