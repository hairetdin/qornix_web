cmake_minimum_required(VERSION 3.20)
project(dynamic_entity_server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add source files
add_executable(dynamic_entity_server
    main.cpp
    ../../server/http_server.cpp
    ../../qornix_orm/core/entity_api_controller.cpp
    ../../qornix_orm/core/handler_interface.cpp
    ../../qornix_orm/core/entity_interface.cpp
    ../../qornix_orm/core/model_interface.cpp
    ../../qornix_orm/core/model_object.cpp
    ../../qornix_orm/core/model_table_proxy.cpp
    ../../qornix_orm/core/entity_base.cpp
    ../../qornix_orm/core/schema_loader.cpp
    ../../qornix_orm/core/xml_schema_validator.cpp
    ../../qornix_orm/core/schema_interface.cpp
    ../../qornix_orm/core/config_interface.cpp
    ../../qornix_orm/database/database_interface.cpp
    ../../qornix_orm/database/table_manager.cpp
    ../../qornix_orm/database/config.cpp
#   ../../qornix_orm/database/drivers/PostgreSQL/PostgreSQLDriver.cpp
#   ../../qornix_orm/database/drivers/MySQL/MySQLDriver.cpp
    ../../qornix_orm/database/drivers/SQLite/SQLiteDriver.cpp
)

# Enable the SQLite driver
target_compile_definitions(dynamic_entity_server PRIVATE
    QORNIX_ENABLE_SQLITE=1
    QORNIX_HAS_LIBXML2
        QORNIX_ORM_SCHEMA_APP_XSD_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_orm/schema/schema_app.xsd"
)

# Set include search directories
target_include_directories(dynamic_entity_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_orm/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_orm/database
    ${PUGIXML_INCLUDE_DIRS}
    ${LIBXML2_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
)

# Find required libraries
find_package(Boost REQUIRED COMPONENTS url json)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PUGIXML REQUIRED pugixml)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# libxml2 is required by qornix_orm/core/xml_schema_validator.cpp.
# This example links ORM sources directly, so it must also add libxml2 includes/libs directly.
target_include_directories(dynamic_entity_server PRIVATE
    ${LIBXML2_INCLUDE_DIRS}
)
target_compile_options(dynamic_entity_server PRIVATE
    ${LIBXML2_CFLAGS_OTHER}
)

# yaml-cpp (unified config format, roadmap v0.0.3)
# The yaml-cpp target is created by root CMakeLists.txt or qornix_orm/CMakeLists.txt.
# Here we just ensure it exists and link against it.
if(NOT TARGET yaml-cpp)
    set(YAML_CPP_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../local/yaml-cpp")
    if(EXISTS "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a" AND EXISTS "${YAML_CPP_LOCAL_DIR}/include/yaml-cpp")
        add_library(yaml-cpp STATIC IMPORTED)
        set_target_properties(yaml-cpp PROPERTIES
            IMPORTED_LOCATION "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a"
            INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_LOCAL_DIR}/include"
        )
    else()
        # Try system package
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(YAML_CPP QUIET yaml-cpp)
        endif()
        if(YAML_CPP_FOUND)
            add_library(yaml-cpp INTERFACE IMPORTED)
            set_target_properties(yaml-cpp PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_INCLUDE_DIRS}"
            )
        else()
            find_package(yaml-cpp QUIET)
            if(yaml-cpp_FOUND)
                target_link_libraries(dynamic_entity_server PRIVATE yaml-cpp::yaml-cpp)
            else()
                message(WARNING "yaml-cpp not found — dynamic_entity_server will link without it")
            endif()
        endif()
    endif()
endif()
target_link_libraries(dynamic_entity_server PRIVATE
    Boost::url
    Boost::json
    ${PUGIXML_LIBRARIES}
    ${LIBXML2_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    sqlite3
    yaml-cpp
)
