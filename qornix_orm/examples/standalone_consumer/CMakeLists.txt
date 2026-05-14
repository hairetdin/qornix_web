cmake_minimum_required(VERSION 3.20)
project(qornix_orm_standalone_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This sample is intentionally outside the main qornix_web CMake tree from the
# consumer point of view.  It verifies that qornix_orm can be consumed as a
# standalone library through add_subdirectory(...).
set(QORNIX_WEB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "Path to the qornix_web repository root")

set(QORNIX_BUILD_ORM_TESTS OFF CACHE BOOL "" FORCE)
set(QORNIX_BUILD_ORM_APP OFF CACHE BOOL "" FORCE)
set(QORNIX_ENABLE_SQLITE ON CACHE BOOL "" FORCE)
set(QORNIX_ENABLE_POSTGRES OFF CACHE BOOL "" FORCE)
set(QORNIX_ENABLE_MYSQL OFF CACHE BOOL "" FORCE)
set(QORNIX_ENABLE_ASYNC_DB ON CACHE BOOL "" FORCE)
set(QORNIX_ENABLE_ASYNC_POSTGRES OFF CACHE BOOL "" FORCE)
set(QORNIX_ENABLE_ASYNC_MYSQL OFF CACHE BOOL "" FORCE)

add_subdirectory("${QORNIX_WEB_ROOT}/qornix_orm" "${CMAKE_BINARY_DIR}/qornix_orm_build")

add_executable(qornix_orm_standalone_consumer main.cpp)
target_link_libraries(qornix_orm_standalone_consumer PRIVATE qornix::orm)
target_compile_features(qornix_orm_standalone_consumer PRIVATE cxx_std_20)
