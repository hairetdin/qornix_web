cmake_minimum_required(VERSION 3.20)
project(qornix_rag)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================
# Phase 4: Universal RAG Module
# ============================================
# qornix_rag can now be used as:
# 1. Static library (qornix_rag_lib) - linked into qornix_web
# 2. Standalone executable (qornix_rag) - independent RAG server
# 3. Dynamic extension (qornix_rag_extension.so) - plugin for qornix_web

# ============================================
# Static Library (always built when QORNIX_BUILD_RAG=ON)
# ============================================
add_library(qornix_rag_lib STATIC
    core.h
    llm_client.cpp
    llm_client.h
    llm_cache.cpp
    llm_cache.h
    rate_limiter.cpp
    rate_limiter.h
    batch_processor.cpp
    batch_processor.h
    prompt_cache.cpp
    prompt_cache.h
    prometheus_metrics.cpp
    prometheus_metrics.h
    web.cpp
    web.h
    data_source.h
    file_source.cpp
    file_source.h
    qa_source.cpp
    qa_source.h
    text_source.cpp
    text_source.h
    memory_source.cpp
    memory_source.h
    sqlite_source.cpp
    sqlite_source.h
    markdown_source.cpp
    markdown_source.h
    analytics_service.cpp
    analytics_service.h
    deduplication_service.cpp
    deduplication_service.h
    rag_extension.cpp
    rag_extension.h
    ../server/http_server.cpp
)

target_include_directories(qornix_rag_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# yaml-cpp library (unified config format)
set(QORNIX_RAG_USE_YAML OFF)

# Try local/ static library first
set(YAML_CPP_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../local/yaml-cpp")
if(EXISTS "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a" AND EXISTS "${YAML_CPP_LOCAL_DIR}/include/yaml-cpp")
    message(STATUS "yaml-cpp found in local/ — YAML config support enabled in qornix_rag")
    add_library(yaml-cpp STATIC IMPORTED)
    set_target_properties(yaml-cpp PROPERTIES
        IMPORTED_LOCATION "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a"
        INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_LOCAL_DIR}/include"
    )
    target_link_libraries(qornix_rag_lib PUBLIC yaml-cpp)
    target_include_directories(qornix_rag_lib PUBLIC "${YAML_CPP_LOCAL_DIR}/include")
    target_compile_definitions(qornix_rag_lib PUBLIC QORNIX_HAS_YAML=1)
    set(QORNIX_RAG_USE_YAML ON)
else()
    # Try system package
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(YAML_CPP QUIET yaml-cpp)
    endif()
    if(YAML_CPP_FOUND)
        target_link_libraries(qornix_rag_lib PUBLIC yaml-cpp)
        target_include_directories(qornix_rag_lib PUBLIC ${YAML_CPP_INCLUDE_DIRS})
        target_compile_definitions(qornix_rag_lib PUBLIC QORNIX_HAS_YAML=1)
        set(QORNIX_RAG_USE_YAML ON)
    else()
        find_package(yaml-cpp QUIET)
        if(yaml-cpp_FOUND)
            target_link_libraries(qornix_rag_lib PUBLIC yaml-cpp::yaml-cpp)
            target_compile_definitions(qornix_rag_lib PUBLIC QORNIX_HAS_YAML=1)
            set(QORNIX_RAG_USE_YAML ON)
        else()
            message(WARNING "yaml-cpp not found — RAG config will use fallback")
        endif()
    endif()
endif()

# HNSWLIB — header-only library
find_path(HNSWLIB_INCLUDE_DIR hnswlib/hnswlib.h
    PATHS /usr/local/include /usr/include
)
if(NOT HNSWLIB_INCLUDE_DIR)
    message(FATAL_ERROR "hnswlib not found. Run qornix_rag/depend_install.sh or install manually.")
endif()
target_include_directories(qornix_rag_lib PRIVATE ${HNSWLIB_INCLUDE_DIR})

# ONNX Runtime (optional)
option(QORNIX_ENABLE_ONNX "Enable ONNX Runtime semantic embeddings" ON)
if(QORNIX_ENABLE_ONNX)
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        PATHS /usr/local/include/onnxruntime /usr/include/onnxruntime
        PATH_SUFFIXES onnxruntime onnxruntime/core/session
    )
    find_library(ONNXRUNTIME_LIBRARY onnxruntime)

    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_LIBRARY}")
        target_compile_definitions(qornix_rag_lib PRIVATE QORNIX_HAS_ONNX=1)
        target_include_directories(qornix_rag_lib PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(qornix_rag_lib PRIVATE ${ONNXRUNTIME_LIBRARY})
    else()
        message(WARNING "ONNX Runtime not found. Build will use TF-IDF fallback.")
    endif()
endif()

# Xapian library
find_package(Xapian REQUIRED)
target_link_libraries(qornix_rag_lib PRIVATE ${XAPIAN_LIBRARIES})
target_include_directories(qornix_rag_lib PRIVATE ${XAPIAN_INCLUDE_DIRS})

# Boost libraries
find_package(Boost 1.83 REQUIRED COMPONENTS filesystem url json)
target_link_libraries(qornix_rag_lib PRIVATE
    Boost::filesystem
    Boost::url
    Boost::json
)

# qornix_web_core (for HttpServer, HandlerBase, etc.)
find_library(QORNIX_WEB_CORE_LIBRARY qornix_web_core
    PATHS ${CMAKE_SOURCE_DIR}/build
)
if(QORNIX_WEB_CORE_LIBRARY)
    target_link_libraries(qornix_rag_lib PRIVATE qornix_web_core)
    message(STATUS "qornix_web_core found — RAG can be integrated into qornix_web")
else()
    message(WARNING "qornix_web_core not found — RAG will build in standalone mode")
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(qornix_rag_lib PRIVATE
        -Wall -Wextra -Wpedantic -O2
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(qornix_rag_lib PRIVATE DEBUG_MODE)
endif()

# Curl library for LLM client (optional)
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(qornix_rag_lib PRIVATE ${CURL_LIBRARIES})
    target_include_directories(qornix_rag_lib PRIVATE ${CURL_INCLUDE_DIRS})
    target_compile_definitions(qornix_rag_lib PRIVATE QORNIX_HAS_CURL=1)
    message(STATUS "CURL found — LLM client enabled")
else()
    message(WARNING "CURL not found — LLM client will be disabled (search-only mode)")
endif()

# ============================================
# SQLite3 library (optional, Phase 5)
# ============================================
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
        target_compile_definitions(qornix_rag_lib PUBLIC QORNIX_HAS_SQLITE=1)
        target_include_directories(qornix_rag_lib PUBLIC ${SQLITE3_INCLUDE_DIR})
        target_link_libraries(qornix_rag_lib PRIVATE ${SQLITE3_LIBRARY})
    else()
        message(WARNING "SQLite3 not found — SQLiteDataSource will be disabled")
    endif()
endif()

# ============================================
# Standalone Executable (optional)
# ============================================
option(QORNIX_BUILD_RAG_STANDALONE "Build standalone qornix_rag executable" ON)
if(QORNIX_BUILD_RAG_STANDALONE)
    add_executable(qornix_rag
        main.cpp
    )
    target_link_libraries(qornix_rag PRIVATE qornix_rag_lib)
    message(STATUS "Standalone qornix_rag executable enabled")
endif()

# ============================================
# Dynamic Extension (optional)
# ============================================
option(QORNIX_BUILD_RAG_EXTENSION "Build qornix_rag as dynamic extension (.so)" OFF)
if(QORNIX_BUILD_RAG_EXTENSION)
    add_library(qornix_rag_extension SHARED
        rag_extension.cpp
    )
    target_link_libraries(qornix_rag_extension PRIVATE qornix_rag_lib)
    set_target_properties(qornix_rag_extension PROPERTIES
        OUTPUT_NAME "qornix_rag_route_extension"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/route_extensions"
    )
    message(STATUS "qornix_rag dynamic extension enabled")
endif()

# ============================================
# Tests
# ============================================
option(QORNIX_BUILD_RAG_TESTS "Build qornix_rag tests" ON)
if(QORNIX_BUILD_RAG_TESTS AND QORNIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
