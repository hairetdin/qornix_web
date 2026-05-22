cmake_minimum_required(VERSION 3.20)
project(qornix_rag)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# qornix_rag target ownership:
#   qornix_rag_core       reusable RAG services and data sources, no HTTP server
#   qornix_rag_http       qornix_web HTTP handlers and route registration
#   qornix_rag_extension  qornix_web extension adapter
#   qornix_rag            standalone executable
#
# qornix_rag_lib is kept as a compatibility alias for existing consumers that
# still link the historical monolithic target name.

set(QORNIX_RAG_CORE_SOURCES
    rag_config.cpp
    rag_service.cpp
    llm_client.cpp
    llm_cache.cpp
    rate_limiter.cpp
    batch_processor.cpp
    prompt_cache.cpp
    prometheus_metrics.cpp
    file_source.cpp
    qa_source.cpp
    text_source.cpp
    memory_source.cpp
    sqlite_source.cpp
    markdown_source.cpp
    analytics_service.cpp
    deduplication_service.cpp
)

set(QORNIX_RAG_CORE_HEADERS
    core.h
    rag_config.h
    rag_service.h
    llm_client.h
    llm_cache.h
    rate_limiter.h
    batch_processor.h
    prompt_cache.h
    prometheus_metrics.h
    data_source.h
    file_source.h
    qa_source.h
    text_source.h
    memory_source.h
    sqlite_source.h
    markdown_source.h
    analytics_service.h
    deduplication_service.h
)

add_library(qornix_rag_core STATIC
    ${QORNIX_RAG_CORE_SOURCES}
    ${QORNIX_RAG_CORE_HEADERS}
)
add_library(qornix::rag_core ALIAS qornix_rag_core)

set_target_properties(qornix_rag_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(qornix_rag_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set(QORNIX_RAG_USE_YAML OFF)
set(QORNIX_RAG_YAML_TARGET "")
set(QORNIX_RAG_YAML_INCLUDE_DIRS "")
set(QORNIX_RAG_YAML_LIBRARIES "")

set(YAML_CPP_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../local/yaml-cpp")
if(EXISTS "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a" AND EXISTS "${YAML_CPP_LOCAL_DIR}/include/yaml-cpp")
    message(STATUS "yaml-cpp found in local/ - YAML config support enabled in qornix_rag")
    if(NOT TARGET qornix_rag_yaml_cpp)
        add_library(qornix_rag_yaml_cpp STATIC IMPORTED)
        set_target_properties(qornix_rag_yaml_cpp PROPERTIES
            IMPORTED_LOCATION "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a"
            INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_LOCAL_DIR}/include"
        )
    endif()
    set(QORNIX_RAG_YAML_TARGET qornix_rag_yaml_cpp)
    set(QORNIX_RAG_USE_YAML ON)
else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(YAML_CPP QUIET yaml-cpp)
    endif()
    if(YAML_CPP_FOUND)
        set(QORNIX_RAG_YAML_INCLUDE_DIRS ${YAML_CPP_INCLUDE_DIRS})
        set(QORNIX_RAG_YAML_LIBRARIES ${YAML_CPP_LIBRARIES})
        set(QORNIX_RAG_USE_YAML ON)
    else()
        find_package(yaml-cpp QUIET)
        if(yaml-cpp_FOUND)
            set(QORNIX_RAG_YAML_TARGET yaml-cpp::yaml-cpp)
            set(QORNIX_RAG_USE_YAML ON)
        else()
            message(WARNING "yaml-cpp not found - RAG config will use fallback")
        endif()
    endif()
endif()

if(QORNIX_RAG_YAML_TARGET)
    target_link_libraries(qornix_rag_core PUBLIC ${QORNIX_RAG_YAML_TARGET})
elseif(QORNIX_RAG_YAML_LIBRARIES)
    target_link_libraries(qornix_rag_core PUBLIC ${QORNIX_RAG_YAML_LIBRARIES})
    target_include_directories(qornix_rag_core PUBLIC ${QORNIX_RAG_YAML_INCLUDE_DIRS})
endif()
if(QORNIX_RAG_USE_YAML)
    target_compile_definitions(qornix_rag_core PUBLIC QORNIX_HAS_YAML=1)
endif()

find_path(HNSWLIB_INCLUDE_DIR hnswlib/hnswlib.h
    PATHS /usr/local/include /usr/include
)
if(NOT HNSWLIB_INCLUDE_DIR)
    message(FATAL_ERROR "hnswlib not found. Run qornix_rag/depend_install.sh or install manually.")
endif()
target_include_directories(qornix_rag_core PRIVATE ${HNSWLIB_INCLUDE_DIR})

option(QORNIX_ENABLE_ONNX "Enable ONNX Runtime semantic embeddings" ON)
if(QORNIX_ENABLE_ONNX)
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        PATHS /usr/local/include/onnxruntime /usr/include/onnxruntime
        PATH_SUFFIXES onnxruntime onnxruntime/core/session
    )
    find_library(ONNXRUNTIME_LIBRARY onnxruntime)

    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_LIBRARY}")
        target_compile_definitions(qornix_rag_core PRIVATE QORNIX_HAS_ONNX=1)
        target_include_directories(qornix_rag_core PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(qornix_rag_core PRIVATE ${ONNXRUNTIME_LIBRARY})
    else()
        message(WARNING "ONNX Runtime not found. Build will use TF-IDF fallback.")
    endif()
endif()

find_package(Xapian REQUIRED)
target_link_libraries(qornix_rag_core PRIVATE ${XAPIAN_LIBRARIES})
target_include_directories(qornix_rag_core PRIVATE ${XAPIAN_INCLUDE_DIRS})

find_package(Boost 1.83 REQUIRED COMPONENTS filesystem url json)
target_link_libraries(qornix_rag_core PRIVATE
    Boost::filesystem
    Boost::json
)

find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(qornix_rag_core PRIVATE ${CURL_LIBRARIES})
    target_include_directories(qornix_rag_core PRIVATE ${CURL_INCLUDE_DIRS})
    target_compile_definitions(qornix_rag_core PUBLIC QORNIX_HAS_CURL=1)
    message(STATUS "CURL found - LLM client enabled")
else()
    message(WARNING "CURL not found - LLM client will be disabled (search-only mode)")
endif()

option(QORNIX_ENABLE_SQLITE "Enable SQLite3 persistent storage" ON)
if(QORNIX_ENABLE_SQLITE)
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h
        PATHS /usr/local/include /usr/include
    )
    find_library(SQLITE3_LIBRARY sqlite3
        PATHS /usr/local/lib /usr/lib
    )

    if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
        message(STATUS "SQLite3 found: ${SQLITE3_LIBRARY}")
        target_compile_definitions(qornix_rag_core PUBLIC QORNIX_HAS_SQLITE=1)
        target_include_directories(qornix_rag_core PUBLIC ${SQLITE3_INCLUDE_DIR})
        target_link_libraries(qornix_rag_core PRIVATE ${SQLITE3_LIBRARY})
    else()
        message(WARNING "SQLite3 not found - SQLiteDataSource will be disabled")
    endif()
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(qornix_rag_core PRIVATE
        -Wall -Wextra -Wpedantic -O2
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(qornix_rag_core PRIVATE DEBUG_MODE)
endif()

add_library(qornix_rag_http STATIC
    web.cpp
    web.h
)
add_library(qornix::rag_http ALIAS qornix_rag_http)
set_target_properties(qornix_rag_http PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(qornix_rag_http PUBLIC qornix_rag_core)
target_include_directories(qornix_rag_http PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if(TARGET qornix_web_core)
    target_link_libraries(qornix_rag_http PUBLIC qornix_web_core)
else()
    find_library(QORNIX_WEB_CORE_LIBRARY qornix_web_core
        PATHS ${CMAKE_SOURCE_DIR}/build
    )
    if(QORNIX_WEB_CORE_LIBRARY)
        target_link_libraries(qornix_rag_http PUBLIC ${QORNIX_WEB_CORE_LIBRARY})
        message(STATUS "qornix_web_core found - RAG HTTP adapter enabled")
    else()
        message(FATAL_ERROR "qornix_web_core target/library is required for qornix_rag_http")
    endif()
endif()

add_library(qornix_rag_extension STATIC
    rag_extension.cpp
    rag_extension.h
)
add_library(qornix::rag_extension ALIAS qornix_rag_extension)
set_target_properties(qornix_rag_extension PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(qornix_rag_extension PUBLIC qornix_rag_http)

add_library(qornix_rag_lib INTERFACE)
target_link_libraries(qornix_rag_lib INTERFACE qornix_rag_extension)

option(QORNIX_BUILD_RAG_STANDALONE "Build standalone qornix_rag executable" ON)
if(QORNIX_BUILD_RAG_STANDALONE)
    add_executable(qornix_rag
        main.cpp
    )
    target_link_libraries(qornix_rag PRIVATE qornix_rag_http)
    message(STATUS "Standalone qornix_rag executable enabled")
endif()

option(QORNIX_BUILD_RAG_EXTENSION "Build qornix_rag as dynamic route extension (.so); legacy option name" OFF)
option(QORNIX_BUILD_RAG_ROUTE_EXTENSION "Build qornix_rag as dynamic route extension (.so)" ${QORNIX_BUILD_RAG_EXTENSION})
if(QORNIX_BUILD_RAG_ROUTE_EXTENSION)
    add_library(qornix_rag_route_extension SHARED
        rag_extension.cpp
    )
    target_link_libraries(qornix_rag_route_extension PRIVATE qornix_rag_http)
    set_target_properties(qornix_rag_route_extension PROPERTIES
        OUTPUT_NAME "qornix_rag_route_extension"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/route_extensions"
    )
    message(STATUS "qornix_rag dynamic route extension enabled")
endif()

option(QORNIX_BUILD_RAG_TESTS "Build qornix_rag tests" ON)
if(QORNIX_BUILD_RAG_TESTS AND QORNIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
