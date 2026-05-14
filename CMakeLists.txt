cmake_minimum_required(VERSION 3.20)
project(qornix_web)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QORNIX_BUILD_APP "Build qornix_web demo application" ON)
option(QORNIX_BUILD_EXAMPLES "Build qornix example applications" OFF)
option(QORNIX_BUILD_RAG "Build qornix_rag application" OFF)
option(QORNIX_BUILD_TESTS "Build qornix_web unit tests" ON)
option(QORNIX_ENABLE_ORM "Enable qornix_orm integration" ON)
option(QORNIX_BUILD_DYNAMIC_API "Build Schema-driven Dynamic API module" ON)
option(ENABLE_AUTH "Enable authentication library" ON)
option(ENABLE_JWT "Enable JWT support in auth library" OFF)

# yaml-cpp library (unified config format, roadmap v0.0.3)
set(QORNIX_WEB_USE_YAML OFF)
set(QORNIX_YAML_TARGET "")
set(QORNIX_YAML_INCLUDE_DIRS "")
set(QORNIX_YAML_LIBRARIES "")

# Try local/static library first.
set(YAML_CPP_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/local/yaml-cpp")
if(EXISTS "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a" AND EXISTS "${YAML_CPP_LOCAL_DIR}/include/yaml-cpp")
    message(STATUS "yaml-cpp found in local/ - YAML config support enabled")
    add_library(qornix_yaml_cpp STATIC IMPORTED)
    set_target_properties(qornix_yaml_cpp PROPERTIES
        IMPORTED_LOCATION "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a"
        INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_LOCAL_DIR}/include"
    )
    set(QORNIX_YAML_TARGET qornix_yaml_cpp)
    set(QORNIX_WEB_USE_YAML ON)
else()
    # Try system package via pkg-config.
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(YAML_CPP QUIET yaml-cpp)
    endif()

    if(YAML_CPP_FOUND)
        set(QORNIX_YAML_INCLUDE_DIRS ${YAML_CPP_INCLUDE_DIRS})
        set(QORNIX_YAML_LIBRARIES ${YAML_CPP_LIBRARIES})
        set(QORNIX_WEB_USE_YAML ON)
    else()
        # Try CMake package.
        find_package(yaml-cpp QUIET)
        if(yaml-cpp_FOUND)
            set(QORNIX_YAML_TARGET yaml-cpp::yaml-cpp)
            set(QORNIX_WEB_USE_YAML ON)
        else()
            message(FATAL_ERROR "yaml-cpp not found. Install with: apt-get install libyaml-cpp-dev")
        endif()
    endif()
endif()

find_package(Boost 1.75 REQUIRED COMPONENTS url json log)

# Optional authentication module.
if(ENABLE_AUTH)
    set(HAVE_JWT_CPP ${ENABLE_JWT} CACHE BOOL "Enable JWT support" FORCE)
    add_subdirectory(qornix_auth)
endif()

if(QORNIX_ENABLE_ORM)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qornix_orm/CMakeLists.txt")
        add_subdirectory(qornix_orm)
    else()
        message(FATAL_ERROR "QORNIX_ENABLE_ORM=ON, but qornix_orm/CMakeLists.txt was not found")
    endif()
endif()

# Core framework library.
# Applications should link against this target instead of compiling framework .cpp files directly.
add_library(qornix_web_core
    server/server_manager.cpp
    server/http_server.cpp
    server/extension_loader.cpp
)

add_library(qornix::web_core ALIAS qornix_web_core)

target_include_directories(qornix_web_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${QORNIX_YAML_INCLUDE_DIRS}
)

target_compile_features(qornix_web_core PUBLIC cxx_std_20)

target_link_libraries(qornix_web_core
    PUBLIC
        Boost::url
        Boost::json
        Boost::log
)

if(QORNIX_YAML_TARGET)
    target_link_libraries(qornix_web_core PUBLIC ${QORNIX_YAML_TARGET})
elseif(QORNIX_YAML_LIBRARIES)
    target_link_libraries(qornix_web_core PUBLIC ${QORNIX_YAML_LIBRARIES})
endif()

if(ENABLE_AUTH)
    target_link_libraries(qornix_web_core PUBLIC qornix_auth)
    if(ENABLE_JWT)
        target_compile_definitions(qornix_web_core PUBLIC HAVE_JWT_CPP)
    endif()
endif()

if(QORNIX_ENABLE_ORM)
    target_link_libraries(qornix_web_core PUBLIC qornix_orm)
endif()

if(QORNIX_BUILD_DYNAMIC_API)
    if(QORNIX_ENABLE_ORM)
        add_subdirectory(qornix_dynamic_api)
    else()
        message(WARNING "QORNIX_BUILD_DYNAMIC_API is ON, but QORNIX_ENABLE_ORM is OFF; qornix_dynamic_api will not be built")
    endif()
endif()

# Demo executable shipped with the framework.
if(QORNIX_BUILD_APP)
    add_executable(qornix_web
        main.cpp
        handlers/user_handler.cpp
    )

    target_include_directories(qornix_web PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/handlers
    )

    target_link_libraries(qornix_web PRIVATE qornix_web_core)
endif()

if(QORNIX_BUILD_EXAMPLES)
    add_subdirectory(example/async_routes_server)
    if(QORNIX_ENABLE_ASYNC_DB)
        add_subdirectory(example/async_orm_server)
    endif()
    if(QORNIX_ENABLE_ASYNC_POSTGRES)
        add_subdirectory(example/async_postgres_server)
    endif()
    if(QORNIX_ENABLE_ASYNC_MYSQL)
        add_subdirectory(example/async_mysql_server)
    endif()
    add_subdirectory(example/dynamic_entity_server)
    add_subdirectory(example/dynamic_web_query_builder_server)
    add_subdirectory(example/schema_driven_backend)
    add_subdirectory(example/wiki)
endif()

if(QORNIX_BUILD_RAG)
    add_subdirectory(qornix_rag)
endif()

# Unit tests.
if(QORNIX_BUILD_TESTS)
    enable_testing()

    add_executable(config_parser_test tests/config_parser_test.cpp)
    target_include_directories(config_parser_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${QORNIX_YAML_INCLUDE_DIRS}
    )
    target_link_libraries(config_parser_test PRIVATE qornix_web_core)
    add_test(NAME config_parser_test COMMAND config_parser_test)

    add_executable(http_server_smoke_test tests/http_server_smoke_test.cpp)
    target_include_directories(http_server_smoke_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(http_server_smoke_test PRIVATE qornix_web_core)
    add_test(NAME http_server_smoke_test COMMAND http_server_smoke_test)

    add_executable(baseline_benchmark_server tests/baseline_benchmark_server.cpp)
    target_include_directories(baseline_benchmark_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(baseline_benchmark_server PRIVATE qornix_web_core)

    if(QORNIX_ENABLE_ORM AND QORNIX_ENABLE_ASYNC_DB)
        add_executable(async_db_benchmark_server tests/async_db_benchmark_server.cpp)
        target_include_directories(async_db_benchmark_server PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/qornix_orm/database
        )
        target_link_libraries(async_db_benchmark_server PRIVATE qornix_web_core)
    endif()

    if(TARGET qornix_dynamic_api)
        add_executable(dynamic_openapi_service_test tests/dynamic_openapi_service_test.cpp)
        target_include_directories(dynamic_openapi_service_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/qornix_dynamic_api
        )
        target_link_libraries(dynamic_openapi_service_test PRIVATE qornix_dynamic_api)
        add_test(NAME dynamic_openapi_service_test COMMAND dynamic_openapi_service_test)

        if(QORNIX_ENABLE_ASYNC_DB)
            add_executable(dynamic_async_query_service_test tests/dynamic_async_query_service_test.cpp)
            target_include_directories(dynamic_async_query_service_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/qornix_dynamic_api
                ${CMAKE_CURRENT_SOURCE_DIR}/qornix_orm/database
            )
            target_link_libraries(dynamic_async_query_service_test PRIVATE qornix_dynamic_api)
            add_test(NAME dynamic_async_query_service_test COMMAND dynamic_async_query_service_test)
            set_tests_properties(dynamic_async_query_service_test PROPERTIES LABELS "dynamic-api;async-db;mock")
        endif()
    endif()
endif()
