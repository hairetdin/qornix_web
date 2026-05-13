cmake_minimum_required(VERSION 3.20)
project(qornix_dynamic_api LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(qornix_dynamic_api STATIC
    dynamic_api_permissions.cpp
    dynamic_schema_allowlist.cpp
    dynamic_query_service.cpp
    schema_service.cpp
    openapi_service.cpp
    dynamic_api_handlers.cpp
)

if(NOT TARGET qornix::dynamic_api)
    add_library(qornix::dynamic_api ALIAS qornix_dynamic_api)
endif()

target_compile_features(qornix_dynamic_api PUBLIC cxx_std_20)

target_include_directories(qornix_dynamic_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(qornix_dynamic_api PUBLIC
    qornix::web_core
    qornix::orm
    Boost::json
)
