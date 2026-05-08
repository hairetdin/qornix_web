cmake_minimum_required(VERSION 3.10)
project(qornix_orm VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Boost 1.75 REQUIRED COMPONENTS json)
if(NOT TARGET Boost::json)
    message(FATAL_ERROR "Boost::json target not found. Please install libboost-json1.83-dev.")
endif()

option(QORNIX_ENABLE_SQLITE "Build qornix_orm SQLite driver" ON)
option(QORNIX_ENABLE_POSTGRES "Build qornix_orm PostgreSQL driver" OFF)
option(QORNIX_ENABLE_MYSQL "Build qornix_orm MySQL driver" OFF)
option(QORNIX_BUILD_ORM_TESTS "Build qornix_orm tests" ON)

if(QORNIX_BUILD_ORM_TESTS AND NOT QORNIX_ENABLE_SQLITE)
    message(FATAL_ERROR "QORNIX_BUILD_ORM_TESTS requires QORNIX_ENABLE_SQLITE because default tests are self-contained SQLite tests.")
endif()

find_package(PkgConfig REQUIRED)

# Check for database libraries
set(QORNIX_DB_LIBRARIES)
set(QORNIX_DB_INCLUDE_DIRS)

if(QORNIX_ENABLE_POSTGRES)
    pkg_check_modules(LIBPQ REQUIRED libpq)
    list(APPEND QORNIX_DB_LIBRARIES ${LIBPQ_LIBRARIES})
    list(APPEND QORNIX_DB_INCLUDE_DIRS ${LIBPQ_INCLUDE_DIRS})
endif()

if(QORNIX_ENABLE_SQLITE)
    pkg_check_modules(SQLITE REQUIRED sqlite3)
    list(APPEND QORNIX_DB_LIBRARIES ${SQLITE_LIBRARIES})
    list(APPEND QORNIX_DB_INCLUDE_DIRS ${SQLITE_INCLUDE_DIRS})
endif()

if(QORNIX_ENABLE_MYSQL)
    pkg_check_modules(MYSQL REQUIRED mysqlclient)
    list(APPEND QORNIX_DB_LIBRARIES ${MYSQL_LIBRARIES})
    list(APPEND QORNIX_DB_INCLUDE_DIRS ${MYSQL_INCLUDE_DIRS})
endif()

# Try to find pugixml (it might not be installed)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PUGIXML QUIET pugixml)
endif()

if(NOT PUGIXML_FOUND)
    # Try alternative method to find pugixml
    find_path(PUGIXML_INCLUDE_DIR NAMES pugixml.hpp)
    find_library(PUGIXML_LIBRARY NAMES pugixml)
    if(PUGIXML_INCLUDE_DIR AND PUGIXML_LIBRARY)
        set(PUGIXML_FOUND TRUE)
        set(PUGIXML_LIBRARIES ${PUGIXML_LIBRARY})
        set(PUGIXML_INCLUDE_DIRS ${PUGIXML_INCLUDE_DIR})
    endif()
endif()

# libxml2 provides XSD validation for qornix_orm/schema/schema_app.xsd.
find_package(LibXml2 QUIET)
if(NOT LibXml2_FOUND AND PKG_CONFIG_FOUND)
    pkg_check_modules(LIBXML2 QUIET libxml-2.0)
    if(LIBXML2_FOUND)
        set(LibXml2_FOUND TRUE)
    endif()
endif()

if(LibXml2_FOUND AND TARGET LibXml2::LibXml2)
    set(LIBXML2_LIBRARIES LibXml2::LibXml2)
endif()
if(LibXml2_FOUND AND NOT LIBXML2_INCLUDE_DIRS AND LIBXML2_INCLUDE_DIR)
    set(LIBXML2_INCLUDE_DIRS ${LIBXML2_INCLUDE_DIR})
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Core source files
set(CORE_SOURCES
    core/config_interface.cpp
    core/handler_interface.cpp
    core/entity_interface.cpp
    core/entity_base.cpp
    core/entity_api_controller.cpp
    core/schema_interface.cpp
    core/schema_loader.cpp
    core/xml_schema_validator.cpp
    core/schema_document.cpp
    core/schema_normalizer.cpp
    core/database_snapshot.cpp
    core/database_schema_exporter.cpp
    core/schema_diff_engine.cpp
    core/schema_risk_policy.cpp
    core/driver_capabilities.cpp
    core/schema_plan.cpp
    core/schema_applier.cpp
    core/schema_history.cpp
    core/model_interface.cpp
    core/model_object.cpp
    core/model_table_proxy.cpp
)

# Database source files
set(DATABASE_SOURCES
    database/database_interface.cpp
    database/table_manager.cpp
    database/query_builder.cpp
    database/config.cpp
)

if(QORNIX_ENABLE_SQLITE)
    list(APPEND DATABASE_SOURCES database/drivers/SQLite/SQLiteDriver.cpp)
endif()

if(QORNIX_ENABLE_MYSQL)
    list(APPEND DATABASE_SOURCES database/drivers/MySQL/MySQLDriver.cpp)
endif()

if(QORNIX_ENABLE_POSTGRES)
    list(APPEND DATABASE_SOURCES database/drivers/PostgreSQL/PostgreSQLDriver.cpp)
endif()

# All source files combined
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${DATABASE_SOURCES}
)

option(QORNIX_BUILD_ORM_APP "Build qornix_orm demo executable" OFF)

# Create the main library
add_library(qornix_orm STATIC ${ALL_SOURCES})
if(NOT TARGET qornix::orm)
    add_library(qornix::orm ALIAS qornix_orm)
endif()

# Link dependencies
target_link_libraries(qornix_orm
    PUBLIC
        Boost::json
        ${QORNIX_DB_LIBRARIES}
)

target_compile_definitions(qornix_orm PUBLIC
    QORNIX_ORM_SCHEMA_APP_XSD_PATH="${CMAKE_CURRENT_SOURCE_DIR}/schema/schema_app.xsd"
    QORNIX_ENABLE_SQLITE=$<BOOL:${QORNIX_ENABLE_SQLITE}>
    QORNIX_ENABLE_POSTGRES=$<BOOL:${QORNIX_ENABLE_POSTGRES}>
    QORNIX_ENABLE_MYSQL=$<BOOL:${QORNIX_ENABLE_MYSQL}>
)

# Add pugixml if found
if(PUGIXML_FOUND)
    target_link_libraries(qornix_orm PUBLIC ${PUGIXML_LIBRARIES})
    target_include_directories(qornix_orm PRIVATE ${PUGIXML_INCLUDE_DIRS})
else()
    message(WARNING "pugixml not found. XML functionality may be limited.")
endif()

# Add libxml2 if found. It is required for XSD validation against schema_app.xsd.
if(LibXml2_FOUND)
    target_link_libraries(qornix_orm PUBLIC ${LIBXML2_LIBRARIES})
    target_include_directories(qornix_orm PRIVATE ${LIBXML2_INCLUDE_DIRS})
    target_compile_definitions(qornix_orm PUBLIC QORNIX_HAS_LIBXML2)
else()
    message(WARNING "libxml2 not found. XSD validation against schema_app.xsd will be unavailable.")
endif()

# Include directories for the library
target_include_directories(qornix_orm PUBLIC
        core
        database
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${QORNIX_DB_INCLUDE_DIRS}
)

# yaml-cpp for unified config format.
#
# qornix_orm can be configured either as part of qornix_web or as a standalone
# project.  When qornix_web already discovered yaml-cpp, reuse the parent
# target/variables.  Otherwise search from the framework root relative to this
# CMakeLists.txt, not from CMAKE_SOURCE_DIR.  This is important for generated
# applications that add qornix_web via add_subdirectory(...): in that case
# CMAKE_SOURCE_DIR points to the application, not to qornix_web.
set(QORNIX_USE_YAML OFF)
get_filename_component(QORNIX_WEB_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(DEFINED QORNIX_YAML_TARGET AND QORNIX_YAML_TARGET AND TARGET ${QORNIX_YAML_TARGET})
    message(STATUS "yaml-cpp inherited from parent target '${QORNIX_YAML_TARGET}' - YAML config support enabled in qornix_orm")
    target_link_libraries(qornix_orm PUBLIC ${QORNIX_YAML_TARGET})
    target_compile_definitions(qornix_orm PUBLIC QORNIX_USE_YAML)
    set(QORNIX_USE_YAML ON)
elseif(TARGET qornix_yaml_cpp)
    message(STATUS "yaml-cpp inherited from parent target 'qornix_yaml_cpp' - YAML config support enabled in qornix_orm")
    target_link_libraries(qornix_orm PUBLIC qornix_yaml_cpp)
    target_compile_definitions(qornix_orm PUBLIC QORNIX_USE_YAML)
    set(QORNIX_USE_YAML ON)
elseif(DEFINED QORNIX_YAML_LIBRARIES AND QORNIX_YAML_LIBRARIES)
    message(STATUS "yaml-cpp inherited from parent variables - YAML config support enabled in qornix_orm")
    target_link_libraries(qornix_orm PUBLIC ${QORNIX_YAML_LIBRARIES})
    target_include_directories(qornix_orm PUBLIC ${QORNIX_YAML_INCLUDE_DIRS})
    target_compile_definitions(qornix_orm PUBLIC QORNIX_USE_YAML)
    set(QORNIX_USE_YAML ON)
else()
    # Try qornix_web/local/yaml-cpp first.
    set(YAML_CPP_LOCAL_DIR "${QORNIX_WEB_ROOT_DIR}/local/yaml-cpp")
    set(YAML_CPP_LOCAL_LIB "")
    set(YAML_CPP_LOCAL_INCLUDE "")

    if(EXISTS "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a")
        set(YAML_CPP_LOCAL_LIB "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a")
    endif()
    if(EXISTS "${YAML_CPP_LOCAL_DIR}/include/yaml-cpp")
        set(YAML_CPP_LOCAL_INCLUDE "${YAML_CPP_LOCAL_DIR}/include")
    endif()

    if(YAML_CPP_LOCAL_LIB AND YAML_CPP_LOCAL_INCLUDE)
        message(STATUS "yaml-cpp found in qornix_web/local/ - YAML config support enabled in qornix_orm")
        if(NOT TARGET qornix_orm_yaml_cpp)
            add_library(qornix_orm_yaml_cpp STATIC IMPORTED)
            set_target_properties(qornix_orm_yaml_cpp PROPERTIES
                IMPORTED_LOCATION "${YAML_CPP_LOCAL_LIB}"
                INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_LOCAL_INCLUDE}"
            )
        endif()
        target_link_libraries(qornix_orm PUBLIC qornix_orm_yaml_cpp)
        target_compile_definitions(qornix_orm PUBLIC QORNIX_USE_YAML)
        set(QORNIX_USE_YAML ON)
    else()
        # Try system CMake package first.
        find_package(yaml-cpp QUIET)
        if(yaml-cpp_FOUND)
            message(STATUS "yaml-cpp found via CMake package - YAML config support enabled in qornix_orm")
            if(TARGET yaml-cpp::yaml-cpp)
                target_link_libraries(qornix_orm PUBLIC yaml-cpp::yaml-cpp)
            elseif(TARGET yaml-cpp)
                target_link_libraries(qornix_orm PUBLIC yaml-cpp)
            else()
                target_link_libraries(qornix_orm PUBLIC yaml-cpp)
            endif()
            target_compile_definitions(qornix_orm PUBLIC QORNIX_USE_YAML)
            set(QORNIX_USE_YAML ON)
        elseif(PKG_CONFIG_FOUND)
            pkg_check_modules(YAML_CPP QUIET yaml-cpp)
            if(YAML_CPP_FOUND)
                message(STATUS "yaml-cpp found via pkg-config - YAML config support enabled in qornix_orm")
                target_link_libraries(qornix_orm PUBLIC ${YAML_CPP_LIBRARIES})
                target_include_directories(qornix_orm PUBLIC ${YAML_CPP_INCLUDE_DIRS})
                target_compile_options(qornix_orm PUBLIC ${YAML_CPP_CFLAGS_OTHER})
                target_compile_definitions(qornix_orm PUBLIC QORNIX_USE_YAML)
                set(QORNIX_USE_YAML ON)
            endif()
        endif()
    endif()
endif()

if(NOT QORNIX_USE_YAML)
    message(WARNING "yaml-cpp not found - falling back to INI config format")
endif()

# Demo executable. Disabled by default when qornix_orm is used as a library dependency.
if(QORNIX_BUILD_ORM_APP)
    add_executable(qornix_orm_main main.cpp)
    target_link_libraries(qornix_orm_main qornix_orm)

    # Output binary next to config.yaml so relative paths work
    set_target_properties(qornix_orm_main PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

if(QORNIX_BUILD_ORM_TESTS)
    enable_testing()

    add_executable(entity_api_controller_test tests/entity_api_controller_test.cpp)
    target_link_libraries(entity_api_controller_test qornix_orm)
    add_test(NAME qornix_orm_entity_api_controller_test COMMAND entity_api_controller_test)
    set_tests_properties(qornix_orm_entity_api_controller_test PROPERTIES LABELS "qornix_orm;sqlite")

    add_executable(table_manager_test tests/table_manager_test.cpp)
    target_link_libraries(table_manager_test qornix_orm)
    add_test(NAME qornix_orm_table_manager_test COMMAND table_manager_test)
    set_tests_properties(qornix_orm_table_manager_test PROPERTIES LABELS "qornix_orm;sqlite")

    add_executable(handler_test tests/handler_test.cpp)
    target_link_libraries(handler_test qornix_orm)
    add_test(NAME qornix_orm_handler_test COMMAND handler_test)
    set_tests_properties(qornix_orm_handler_test PROPERTIES LABELS "qornix_orm;sqlite")

    add_executable(query_builder_test tests/query_builder_test.cpp)
    target_link_libraries(query_builder_test qornix_orm)
    add_test(NAME qornix_orm_query_builder_test COMMAND query_builder_test)
    set_tests_properties(qornix_orm_query_builder_test PROPERTIES LABELS "qornix_orm;sqlite")

    if(QORNIX_ENABLE_SQLITE)
        add_executable(database_snapshot_test tests/database_snapshot_test.cpp)
        target_link_libraries(database_snapshot_test qornix_orm)
        add_test(NAME qornix_orm_database_snapshot_test COMMAND database_snapshot_test)
        set_tests_properties(qornix_orm_database_snapshot_test PROPERTIES LABELS "qornix_orm;sqlite;schema-snapshot")
    endif()

    add_executable(database_schema_exporter_test tests/database_schema_exporter_test.cpp)
    target_link_libraries(database_schema_exporter_test qornix_orm)
    target_compile_definitions(database_schema_exporter_test PRIVATE
        QORNIX_ORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME qornix_orm_database_schema_exporter_test COMMAND database_schema_exporter_test)
    set_tests_properties(qornix_orm_database_schema_exporter_test PROPERTIES LABELS "qornix_orm;schema-export;schema-document")

    add_executable(config_yaml_test tests/config_yaml_test.cpp)
    target_link_libraries(config_yaml_test qornix_orm)
    add_test(NAME qornix_orm_config_yaml_test COMMAND config_yaml_test)
    set_tests_properties(qornix_orm_config_yaml_test PROPERTIES LABELS "qornix_orm;yaml")

    if(LibXml2_FOUND)
        add_executable(xml_schema_validator_test tests/xml_schema_validator_test.cpp)
        target_link_libraries(xml_schema_validator_test qornix_orm)
        target_compile_definitions(xml_schema_validator_test PRIVATE
            QORNIX_ORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME qornix_orm_xml_schema_validator_test COMMAND xml_schema_validator_test)
        set_tests_properties(qornix_orm_xml_schema_validator_test PROPERTIES LABELS "qornix_orm;xml;xsd")

        add_executable(schema_document_test tests/schema_document_test.cpp)
        target_link_libraries(schema_document_test qornix_orm)
        target_compile_definitions(schema_document_test PRIVATE
            QORNIX_ORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME qornix_orm_schema_document_test COMMAND schema_document_test)
        set_tests_properties(qornix_orm_schema_document_test PROPERTIES LABELS "qornix_orm;xml;xsd;schema-document")

        add_executable(schema_normalizer_test tests/schema_normalizer_test.cpp)
        target_link_libraries(schema_normalizer_test qornix_orm)
        target_compile_definitions(schema_normalizer_test PRIVATE
            QORNIX_ORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME qornix_orm_schema_normalizer_test COMMAND schema_normalizer_test)
        set_tests_properties(qornix_orm_schema_normalizer_test PROPERTIES LABELS "qornix_orm;xml;xsd;schema-document;normalization")

        add_executable(schema_diff_engine_test tests/schema_diff_engine_test.cpp)
        target_link_libraries(schema_diff_engine_test qornix_orm)
        target_compile_definitions(schema_diff_engine_test PRIVATE
            QORNIX_ORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME qornix_orm_schema_diff_engine_test COMMAND schema_diff_engine_test)
        set_tests_properties(qornix_orm_schema_diff_engine_test PROPERTIES LABELS "qornix_orm;xml;xsd;schema-document;schema-diff")

        add_executable(schema_risk_policy_test tests/schema_risk_policy_test.cpp)
        target_link_libraries(schema_risk_policy_test qornix_orm)
        target_compile_definitions(schema_risk_policy_test PRIVATE
            QORNIX_ORM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME qornix_orm_schema_risk_policy_test COMMAND schema_risk_policy_test)
        set_tests_properties(qornix_orm_schema_risk_policy_test PROPERTIES LABELS "qornix_orm;schema-diff;schema-risk;schema-policy")

        add_executable(driver_capabilities_test tests/driver_capabilities_test.cpp)
        target_link_libraries(driver_capabilities_test qornix_orm)
        add_test(NAME qornix_orm_driver_capabilities_test COMMAND driver_capabilities_test)
        set_tests_properties(qornix_orm_driver_capabilities_test PROPERTIES LABELS "qornix_orm;schema-diff;driver-capabilities")

        add_executable(schema_plan_test tests/schema_plan_test.cpp)
        target_link_libraries(schema_plan_test qornix_orm)
        add_test(NAME qornix_orm_schema_plan_test COMMAND schema_plan_test)
        set_tests_properties(qornix_orm_schema_plan_test PROPERTIES LABELS "qornix_orm;schema-diff;schema-plan")

        add_executable(schema_applier_test tests/schema_applier_test.cpp)
        target_link_libraries(schema_applier_test qornix_orm)
        add_test(NAME qornix_orm_schema_applier_test COMMAND schema_applier_test)
        set_tests_properties(qornix_orm_schema_applier_test PROPERTIES LABELS "qornix_orm;schema-plan;schema-apply")

        add_executable(schema_history_test tests/schema_history_test.cpp)
        target_link_libraries(schema_history_test qornix_orm)
        add_test(NAME qornix_orm_schema_history_test COMMAND schema_history_test)
        set_tests_properties(qornix_orm_schema_history_test PROPERTIES LABELS "qornix_orm;schema-plan;schema-history")
    endif()

    if(QORNIX_ENABLE_MYSQL)
        add_executable(mysql_driver_test tests/mysql_driver_test.cpp)
        target_link_libraries(mysql_driver_test qornix_orm)
        add_test(NAME qornix_orm_mysql_driver_test COMMAND mysql_driver_test)
        set_tests_properties(qornix_orm_mysql_driver_test PROPERTIES LABELS "qornix_orm;integration-mysql")
    endif()

    if(QORNIX_ENABLE_POSTGRES)
        add_executable(postgres_driver_test tests/postgres_driver_test.cpp)
        target_link_libraries(postgres_driver_test qornix_orm)
        add_test(NAME qornix_orm_postgres_driver_test COMMAND postgres_driver_test)
        set_tests_properties(qornix_orm_postgres_driver_test PROPERTIES LABELS "qornix_orm;integration-postgres")
    endif()
endif()
