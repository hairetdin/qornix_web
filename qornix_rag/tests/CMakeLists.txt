# qornix_rag tests CMakeLists.txt

option(QORNIX_RAG_BUILD_TESTS "Build qornix_rag tests" ON)

if(NOT QORNIX_RAG_BUILD_TESTS)
    return()
endif()

enable_testing()

# Find qornix_web_core library
set(QORNIX_WEB_CORE_LIB "")
set(QORNIX_WEB_CORE_INCLUDE "")

# Try common build locations
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../build/async-gate-core/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../build/async-gate-core/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../build/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../build/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
endif()

if(NOT QORNIX_WEB_CORE_LIB)
    message(WARNING "qornix_web_core library not found. Tests will not build.")
    return()
endif()

# Helper macro for yaml-cpp discovery (mirrors parent CMakeLists.txt)
# Reuses the IMPORTED target created by parent if available, otherwise finds independently.
macro(find_yaml_cpp_for target_name)
    # If parent already created the yaml-cpp IMPORTED target, just link it
    if(TARGET yaml-cpp)
        target_link_libraries(${target_name} PRIVATE yaml-cpp)
        target_compile_definitions(${target_name} PRIVATE QORNIX_HAS_YAML=1)
        return()
    endif()

    set(YAML_CPP_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../local/yaml-cpp")
    if(EXISTS "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a" AND EXISTS "${YAML_CPP_LOCAL_DIR}/include/yaml-cpp")
        message(STATUS "yaml-cpp found in local/ — enabling YAML support for ${target_name}")
        add_library(yaml-cpp STATIC IMPORTED)
        set_target_properties(yaml-cpp PROPERTIES
            IMPORTED_LOCATION "${YAML_CPP_LOCAL_DIR}/build/libyaml-cpp.a"
            INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_LOCAL_DIR}/include"
        )
        target_link_libraries(${target_name} PRIVATE yaml-cpp)
        target_include_directories(${target_name} PRIVATE "${YAML_CPP_LOCAL_DIR}/include")
        target_compile_definitions(${target_name} PRIVATE QORNIX_HAS_YAML=1)
    else()
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(YAML_CPP QUIET yaml-cpp)
        endif()
        if(YAML_CPP_FOUND)
            target_link_libraries(${target_name} PRIVATE yaml-cpp)
            target_include_directories(${target_name} PRIVATE ${YAML_CPP_INCLUDE_DIRS})
            target_compile_definitions(${target_name} PRIVATE QORNIX_HAS_YAML=1)
        else()
            find_package(yaml-cpp QUIET)
            if(yaml-cpp_FOUND)
                target_link_libraries(${target_name} PRIVATE yaml-cpp::yaml-cpp)
                target_compile_definitions(${target_name} PRIVATE QORNIX_HAS_YAML=1)
            else()
                message(WARNING "yaml-cpp not found for ${target_name} — YAML config tests will fall back")
            endif()
        endif()
    endif()
endmacro()

# Helper macro for test targets with optional curl and yaml-cpp
macro(add_rag_test test_name source_file)
    add_executable(${test_name}
        ${source_file}
        mocks/mock_llm_server.cpp
    )

    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${QORNIX_WEB_CORE_INCLUDE}
    )

    if(CURL_FOUND)
        target_link_libraries(${test_name} PRIVATE
            ${QORNIX_WEB_CORE_LIB}
            ${CURL_LIBRARIES}
        )
        target_compile_definitions(${test_name} PRIVATE QORNIX_HAS_CURL=1)
    endif()

    find_yaml_cpp_for(${test_name})

    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

# Test executable
add_rag_test(test_llm_client test_llm_client.cpp)
target_sources(test_llm_client PRIVATE ../llm_client.cpp)

# API tests
add_rag_test(test_rag_api test_rag_api.cpp)

# Graceful degradation tests
add_rag_test(test_graceful_degradation test_graceful_degradation.cpp)

# Health check tests
add_rag_test(test_health_check test_health_check.cpp)

# Streaming tests
add_rag_test(test_streaming test_streaming.cpp)

# Prompt builder tests
add_executable(test_prompt_builder
    test_prompt_builder.cpp
)

target_include_directories(test_prompt_builder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)

target_link_libraries(test_prompt_builder PRIVATE
    ${QORNIX_WEB_CORE_LIB}
)

if(CURL_FOUND)
    target_link_libraries(test_prompt_builder PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(test_prompt_builder PRIVATE QORNIX_HAS_CURL=1)
endif()

find_yaml_cpp_for(test_prompt_builder)

add_test(NAME test_prompt_builder COMMAND test_prompt_builder)

# Phase 3 tests (cache & rate limiting)
add_executable(test_phase3
    test_phase3.cpp
)

target_include_directories(test_phase3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)

target_link_libraries(test_phase3 PRIVATE
    ${QORNIX_WEB_CORE_LIB}
)

if(CURL_FOUND)
    target_link_libraries(test_phase3 PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(test_phase3 PRIVATE QORNIX_HAS_CURL=1)
endif()

find_yaml_cpp_for(test_phase3)

# Link implementation files
target_sources(test_phase3 PRIVATE
    ../llm_cache.cpp
    ../rate_limiter.cpp
)

add_test(NAME test_phase3 COMMAND test_phase3)

# Phase 3 Extended tests (batch, prompt cache, prometheus metrics)
add_executable(test_phase3_extended
    test_phase3_extended.cpp
)

target_include_directories(test_phase3_extended PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)

target_link_libraries(test_phase3_extended PRIVATE
    ${QORNIX_WEB_CORE_LIB}
)

if(CURL_FOUND)
    target_link_libraries(test_phase3_extended PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(test_phase3_extended PRIVATE QORNIX_HAS_CURL=1)
endif()

find_yaml_cpp_for(test_phase3_extended)

# Link implementation files
target_sources(test_phase3_extended PRIVATE
    ../batch_processor.cpp
    ../prompt_cache.cpp
    ../prometheus_metrics.cpp
)

add_test(NAME test_phase3_extended COMMAND test_phase3_extended)

# Phase 4: DataSource tests
add_executable(test_data_sources
    test_data_sources.cpp
)

target_include_directories(test_data_sources PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)

target_link_libraries(test_data_sources PRIVATE
    ${QORNIX_WEB_CORE_LIB}
)

if(CURL_FOUND)
    target_link_libraries(test_data_sources PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(test_data_sources PRIVATE QORNIX_HAS_CURL=1)
endif()

find_yaml_cpp_for(test_data_sources)

# Link implementation files
target_sources(test_data_sources PRIVATE
    ../memory_source.cpp
    ../qa_source.cpp
    ../file_source.cpp
    ../text_source.cpp
)

add_test(NAME test_data_sources COMMAND test_data_sources)

# Print test summary
message(STATUS "qornix_rag tests enabled")
message(STATUS "  - test_llm_client")
message(STATUS "  - test_rag_api")
message(STATUS "  - test_graceful_degradation")
message(STATUS "  - test_health_check")
message(STATUS "  - test_streaming")
message(STATUS "  - test_prompt_builder")
message(STATUS "  - test_phase3 (cache & rate limiting)")
message(STATUS "  - test_phase3_extended (batch, prompt cache, prometheus metrics)")
message(STATUS "  - test_data_sources (DataSource implementations)")
