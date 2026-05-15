cmake_minimum_required(VERSION 3.20)
project(dynamic_web_query_builder_server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS url json log)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE REQUIRED sqlite3)

# This example consumes framework modules as libraries instead of compiling their sources directly.
if(NOT TARGET qornix::web_core)
    message(FATAL_ERROR "dynamic_web_query_builder_server must be built from the qornix_web root CMake project so qornix::web_core is available")
endif()
if(NOT TARGET qornix::orm)
    message(FATAL_ERROR "dynamic_web_query_builder_server requires qornix::orm. Configure with QORNIX_ENABLE_ORM=ON")
endif()
if(NOT TARGET qornix::dynamic_api)
    message(FATAL_ERROR "dynamic_web_query_builder_server requires qornix::dynamic_api. Configure with QORNIX_BUILD_DYNAMIC_API=ON")
endif()

add_executable(dynamic_web_query_builder_server
    main.cpp
)

target_compile_definitions(dynamic_web_query_builder_server PRIVATE
    DYNAMIC_WEB_QUERY_BUILDER_SERVER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    QORNIX_ENABLE_SQLITE=1
)

target_include_directories(dynamic_web_query_builder_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_dynamic_api
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_orm/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_orm/database
    ${SQLITE_INCLUDE_DIRS}
)

target_link_libraries(dynamic_web_query_builder_server PRIVATE
    qornix::web_core
    qornix::orm
    qornix::dynamic_api
    Boost::url
    Boost::json
    Boost::log
    ${SQLITE_LIBRARIES}
)

add_executable(dynamic_web_query_builder_schema_roundtrip_test
    schema_roundtrip_test.cpp
)

target_compile_definitions(dynamic_web_query_builder_schema_roundtrip_test PRIVATE
    DYNAMIC_WEB_QUERY_BUILDER_SERVER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    QORNIX_ENABLE_SQLITE=1
)

target_include_directories(dynamic_web_query_builder_schema_roundtrip_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_orm/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_orm/database
    ${SQLITE_INCLUDE_DIRS}
)

target_link_libraries(dynamic_web_query_builder_schema_roundtrip_test PRIVATE
    qornix::orm
    Boost::json
    ${SQLITE_LIBRARIES}
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml"
    "${CMAKE_CURRENT_BINARY_DIR}/config.yaml"
    COPYONLY
)
