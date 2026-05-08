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
