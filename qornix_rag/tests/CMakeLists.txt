# qornix_rag tests CMakeLists.txt

option(QORNIX_RAG_BUILD_TESTS "Build qornix_rag tests" ON)

if(NOT QORNIX_RAG_BUILD_TESTS)
    return()
endif()

enable_testing()

# Find qornix_web_core library
set(QORNIX_WEB_CORE_LIB "")
set(QORNIX_WEB_CORE_INCLUDE "")
set(QORNIX_WEB_CORE_LINK_TARGET "")
set(QORNIX_RAG_CORE_LINK_TARGET "")

if(TARGET qornix_rag_core)
    set(QORNIX_RAG_CORE_LINK_TARGET qornix_rag_core)
endif()

# Prefer the parent CMake target when tests are built as part of qornix_web.
if(TARGET qornix_web_core)
    set(QORNIX_WEB_CORE_LINK_TARGET qornix_web_core)
    set(QORNIX_WEB_CORE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
# Try common standalone build locations.
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../build/async-gate-core/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../build/async-gate-core/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../build/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../build/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake-build-debug/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake-build-debug/libqornix_web_core.a")
    set(QORNIX_WEB_CORE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
endif()

if(NOT QORNIX_WEB_CORE_LINK_TARGET AND NOT QORNIX_WEB_CORE_LIB)
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

    target_link_libraries(${test_name} PRIVATE
        ${QORNIX_WEB_CORE_LINK_TARGET}
        ${QORNIX_WEB_CORE_LIB}
        ${QORNIX_RAG_CORE_LINK_TARGET}
    )
    if(TARGET Boost::json)
        target_link_libraries(${test_name} PRIVATE Boost::json)
    endif()

    if(CURL_FOUND)
        target_link_libraries(${test_name} PRIVATE ${CURL_LIBRARIES})
        target_compile_definitions(${test_name} PRIVATE QORNIX_HAS_CURL=1)
    endif()

    find_yaml_cpp_for(${test_name})

    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

# Test executable
add_rag_test(test_llm_client test_llm_client.cpp)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_llm_client PRIVATE
        ../llm_client.cpp
        ../llm_cache.cpp
        ../rate_limiter.cpp
    )
endif()

# API tests
add_rag_test(test_rag_api test_rag_api.cpp)

add_executable(test_ingestion_pipeline
    test_ingestion_pipeline.cpp
)
target_include_directories(test_ingestion_pipeline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(test_ingestion_pipeline PRIVATE
    ${QORNIX_RAG_CORE_LINK_TARGET}
)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_ingestion_pipeline PRIVATE
        ../ingestion_pipeline.cpp
    )
endif()
add_test(NAME test_ingestion_pipeline COMMAND test_ingestion_pipeline)

add_executable(test_document_chunker
    test_document_chunker.cpp
)
target_include_directories(test_document_chunker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(test_document_chunker PRIVATE
    ${QORNIX_RAG_CORE_LINK_TARGET}
)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_document_chunker PRIVATE
        ../document_chunker.cpp
    )
endif()
add_test(NAME test_document_chunker COMMAND test_document_chunker)

add_executable(test_vector_store
    test_vector_store.cpp
)
target_include_directories(test_vector_store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(test_vector_store PRIVATE
    ${QORNIX_RAG_CORE_LINK_TARGET}
)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_vector_store PRIVATE
        ../vector_store.cpp
    )
endif()
add_test(NAME test_vector_store COMMAND test_vector_store)

add_executable(test_vector_store_live
    test_vector_store_live.cpp
)
target_include_directories(test_vector_store_live PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(test_vector_store_live PRIVATE
    ${QORNIX_RAG_CORE_LINK_TARGET}
)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_vector_store_live PRIVATE
        ../vector_store.cpp
    )
endif()
add_test(NAME test_vector_store_live COMMAND test_vector_store_live)
set_tests_properties(test_vector_store_live PROPERTIES LABELS "live;vector-store")

if(QORNIX_RAG_FAISS_AVAILABLE)
    add_executable(test_faiss_vector_store
        test_faiss_vector_store.cpp
    )
    target_include_directories(test_faiss_vector_store PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_link_libraries(test_faiss_vector_store PRIVATE
        ${QORNIX_RAG_CORE_LINK_TARGET}
    )
    add_test(NAME test_faiss_vector_store COMMAND test_faiss_vector_store)
    set_tests_properties(test_faiss_vector_store PROPERTIES LABELS "faiss;vector-store")
endif()

add_executable(test_embedding_config
    test_embedding_config.cpp
)
target_include_directories(test_embedding_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(test_embedding_config PRIVATE
    ${QORNIX_RAG_CORE_LINK_TARGET}
)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_embedding_config PRIVATE
        ../rag_config.cpp
    )
endif()
find_yaml_cpp_for(test_embedding_config)
add_test(NAME test_embedding_config COMMAND test_embedding_config)

add_executable(test_upload_validation
    test_upload_validation.cpp
)
target_include_directories(test_upload_validation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(test_upload_validation PRIVATE
    ${QORNIX_RAG_CORE_LINK_TARGET}
)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_upload_validation PRIVATE
        ../rag_upload.cpp
        ../ingestion_pipeline.cpp
    )
endif()
add_test(NAME test_upload_validation COMMAND test_upload_validation)

add_executable(test_rag_service
    test_rag_service.cpp
)
target_include_directories(test_rag_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)
target_link_libraries(test_rag_service PRIVATE
    ${QORNIX_RAG_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_rag_service PRIVATE
        ../rag_service.cpp
        ../rag_config.cpp
        ../llm_client.cpp
        ../llm_cache.cpp
        ../rate_limiter.cpp
        ../file_source.cpp
        ../qa_source.cpp
        ../text_source.cpp
        ../memory_source.cpp
        ../sqlite_source.cpp
    )
endif()
if(SQLITE3_LIBRARY)
    target_link_libraries(test_rag_service PRIVATE ${SQLITE3_LIBRARY})
    target_include_directories(test_rag_service PRIVATE ${SQLITE3_INCLUDE_DIR})
    target_compile_definitions(test_rag_service PRIVATE QORNIX_HAS_SQLITE=1)
endif()
find_yaml_cpp_for(test_rag_service)
add_test(NAME test_rag_service COMMAND test_rag_service)

add_executable(test_rag_quality_eval
    test_rag_quality_eval.cpp
)
target_include_directories(test_rag_quality_eval PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)
target_link_libraries(test_rag_quality_eval PRIVATE
    ${QORNIX_RAG_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    Boost::json
)
target_compile_definitions(test_rag_quality_eval PRIVATE
    QORNIX_RAG_TEST_EVAL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/eval"
)
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_rag_quality_eval PRIVATE
        ../rag_service.cpp
        ../rag_config.cpp
        ../llm_client.cpp
        ../llm_cache.cpp
        ../rate_limiter.cpp
        ../file_source.cpp
        ../qa_source.cpp
        ../text_source.cpp
        ../memory_source.cpp
        ../sqlite_source.cpp
    )
endif()
if(SQLITE3_LIBRARY)
    target_link_libraries(test_rag_quality_eval PRIVATE ${SQLITE3_LIBRARY})
    target_include_directories(test_rag_quality_eval PRIVATE ${SQLITE3_INCLUDE_DIR})
    target_compile_definitions(test_rag_quality_eval PRIVATE QORNIX_HAS_SQLITE=1)
endif()
find_yaml_cpp_for(test_rag_quality_eval)
add_test(NAME test_rag_quality_eval COMMAND test_rag_quality_eval)

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
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    ${QORNIX_RAG_CORE_LINK_TARGET}
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
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    ${QORNIX_RAG_CORE_LINK_TARGET}
)

if(CURL_FOUND)
    target_link_libraries(test_phase3 PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(test_phase3 PRIVATE QORNIX_HAS_CURL=1)
endif()

find_yaml_cpp_for(test_phase3)

# Link implementation files
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_phase3 PRIVATE
        ../llm_cache.cpp
        ../rate_limiter.cpp
    )
endif()

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
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    ${QORNIX_RAG_CORE_LINK_TARGET}
)

if(CURL_FOUND)
    target_link_libraries(test_phase3_extended PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(test_phase3_extended PRIVATE QORNIX_HAS_CURL=1)
endif()

find_yaml_cpp_for(test_phase3_extended)

# Link implementation files
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_phase3_extended PRIVATE
        ../batch_processor.cpp
        ../prompt_cache.cpp
        ../prometheus_metrics.cpp
    )
endif()

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
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    ${QORNIX_RAG_CORE_LINK_TARGET}
)

if(CURL_FOUND)
    target_link_libraries(test_data_sources PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(test_data_sources PRIVATE QORNIX_HAS_CURL=1)
endif()

find_yaml_cpp_for(test_data_sources)

# Link implementation files
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_data_sources PRIVATE
        ../memory_source.cpp
        ../qa_source.cpp
        ../file_source.cpp
        ../text_source.cpp
    )
endif()

add_test(NAME test_data_sources COMMAND test_data_sources)

# Phase 5: SQLiteSource tests
add_executable(test_sqlite_source
    test_sqlite_source.cpp
)

target_include_directories(test_sqlite_source PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)

target_link_libraries(test_sqlite_source PRIVATE
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    ${QORNIX_RAG_CORE_LINK_TARGET}
)

find_yaml_cpp_for(test_sqlite_source)

# Link implementation files
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_sqlite_source PRIVATE
        ../sqlite_source.cpp
    )
endif()

if(SQLITE3_LIBRARY)
    target_link_libraries(test_sqlite_source PRIVATE ${SQLITE3_LIBRARY})
    target_include_directories(test_sqlite_source PRIVATE ${SQLITE3_INCLUDE_DIR})
    target_compile_definitions(test_sqlite_source PRIVATE QORNIX_HAS_SQLITE=1)
endif()

add_test(NAME test_sqlite_source COMMAND test_sqlite_source)

# Phase 5: MarkdownSource tests
add_executable(test_markdown_source
    test_markdown_source.cpp
)

target_include_directories(test_markdown_source PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)

target_link_libraries(test_markdown_source PRIVATE
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    ${QORNIX_RAG_CORE_LINK_TARGET}
)

find_yaml_cpp_for(test_markdown_source)

# Link implementation files
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_markdown_source PRIVATE
        ../markdown_source.cpp
    )
endif()

add_test(NAME test_markdown_source COMMAND test_markdown_source)

# Phase 5: DeduplicationService tests
add_executable(test_deduplication
    test_deduplication.cpp
)

target_include_directories(test_deduplication PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)

target_link_libraries(test_deduplication PRIVATE
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    ${QORNIX_RAG_CORE_LINK_TARGET}
)

find_yaml_cpp_for(test_deduplication)

# Link implementation files
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_deduplication PRIVATE
        ../deduplication_service.cpp
        ../sqlite_source.cpp
    )
endif()

if(SQLITE3_LIBRARY)
    target_link_libraries(test_deduplication PRIVATE ${SQLITE3_LIBRARY})
    target_include_directories(test_deduplication PRIVATE ${SQLITE3_INCLUDE_DIR})
    target_compile_definitions(test_deduplication PRIVATE QORNIX_HAS_SQLITE=1)
endif()

add_test(NAME test_deduplication COMMAND test_deduplication)

# Phase 5: AnalyticsService tests
add_executable(test_analytics
    test_analytics.cpp
)

target_include_directories(test_analytics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${QORNIX_WEB_CORE_INCLUDE}
)

target_link_libraries(test_analytics PRIVATE
    ${QORNIX_WEB_CORE_LINK_TARGET}
    ${QORNIX_WEB_CORE_LIB}
    ${QORNIX_RAG_CORE_LINK_TARGET}
)

find_yaml_cpp_for(test_analytics)

# Link implementation files
if(NOT QORNIX_RAG_CORE_LINK_TARGET)
    target_sources(test_analytics PRIVATE
        ../analytics_service.cpp
    )
endif()

add_test(NAME test_analytics COMMAND test_analytics)

# Print test summary
message(STATUS "qornix_rag tests enabled")
message(STATUS "  - test_llm_client")
message(STATUS "  - test_rag_api")
message(STATUS "  - test_ingestion_pipeline")
message(STATUS "  - test_document_chunker")
message(STATUS "  - test_vector_store")
message(STATUS "  - test_embedding_config")
message(STATUS "  - test_upload_validation")
message(STATUS "  - test_rag_service")
message(STATUS "  - test_rag_quality_eval")
message(STATUS "  - test_graceful_degradation")
message(STATUS "  - test_health_check")
message(STATUS "  - test_streaming")
message(STATUS "  - test_prompt_builder")
message(STATUS "  - test_phase3 (cache & rate limiting)")
message(STATUS "  - test_phase3_extended (batch, prompt cache, prometheus metrics)")
message(STATUS "  - test_data_sources (DataSource implementations)")
message(STATUS "  - test_sqlite_source (SQLite persistent storage)")
message(STATUS "  - test_markdown_source (Markdown import)")
message(STATUS "  - test_deduplication (Semantic deduplication)")
message(STATUS "  - test_analytics (Search analytics)")
