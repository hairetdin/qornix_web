cmake_minimum_required(VERSION 3.20)
project(qornix_rag)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(qornix_rag
    main.cpp
    ../server/http_server.cpp
)

target_include_directories(qornix_rag PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# yaml-cpp library (unified config format, roadmap v0.0.3)
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
    target_link_libraries(qornix_rag PRIVATE yaml-cpp)
    target_include_directories(qornix_rag PRIVATE "${YAML_CPP_LOCAL_DIR}/include")
    set(QORNIX_RAG_USE_YAML ON)
else()
    # Try system package
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(YAML_CPP QUIET yaml-cpp)
    endif()
    if(YAML_CPP_FOUND)
        target_link_libraries(qornix_rag PRIVATE yaml-cpp)
        target_include_directories(qornix_rag PRIVATE ${YAML_CPP_INCLUDE_DIRS})
        set(QORNIX_RAG_USE_YAML ON)
    else()
        find_package(yaml-cpp QUIET)
        if(yaml-cpp_FOUND)
            target_link_libraries(qornix_rag PRIVATE yaml-cpp::yaml-cpp)
            set(QORNIX_RAG_USE_YAML ON)
        else()
            message(WARNING "yaml-cpp not found — RAG config will use fallback")
        endif()
    endif()
endif()

# HNSWLIB — header-only library, locate headers directly
find_path(HNSWLIB_INCLUDE_DIR hnswlib/hnswlib.h
    PATHS /usr/local/include /usr/include
)
if(NOT HNSWLIB_INCLUDE_DIR)
    message(FATAL_ERROR "hnswlib not found. Run qornix_rag/depend_install.sh or install manually.")
endif()
target_include_directories(qornix_rag PRIVATE ${HNSWLIB_INCLUDE_DIR})

option(QORNIX_ENABLE_ONNX "Enable ONNX Runtime semantic embeddings" ON)
if(QORNIX_ENABLE_ONNX)
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        PATHS /usr/local/include/onnxruntime /usr/include/onnxruntime
        PATH_SUFFIXES onnxruntime onnxruntime/core/session
    )
    find_library(ONNXRUNTIME_LIBRARY onnxruntime)

    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_LIBRARY}")
        target_compile_definitions(qornix_rag PRIVATE QORNIX_HAS_ONNX=1)
        target_include_directories(qornix_rag PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(qornix_rag PRIVATE ${ONNXRUNTIME_LIBRARY})
    else()
        message(WARNING "ONNX Runtime not found. Build will use TF-IDF fallback.")
    endif()
endif()

# Xapian library
find_package(Xapian REQUIRED)
target_link_libraries(qornix_rag PRIVATE ${XAPIAN_LIBRARIES})
target_include_directories(qornix_rag PRIVATE ${XAPIAN_INCLUDE_DIRS})

# Boost libraries
find_package(Boost 1.75 REQUIRED COMPONENTS filesystem url json)
target_link_libraries(qornix_rag PRIVATE
    Boost::filesystem
    Boost::url
    Boost::json
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(qornix_rag PRIVATE
        -Wall -Wextra -Wpedantic -O2
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(qornix_rag PRIVATE DEBUG_MODE)
endif()
