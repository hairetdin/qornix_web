cmake_minimum_required(VERSION 3.20)
project(qornix_auth)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HAVE_JWT_CPP "Enable JWT support" OFF)

find_package(OpenSSL REQUIRED)

find_package(Boost 1.83 REQUIRED COMPONENTS json url)
if(NOT TARGET Boost::json)
    message(FATAL_ERROR "Boost::json target not found. Please install libboost-json1.83-dev.")
endif()

if(HAVE_JWT_CPP)
    find_package(jwt-cpp CONFIG QUIET)
    if(NOT jwt-cpp_FOUND)
        message(WARNING "jwt-cpp not found, JWT support will be disabled")
        set(HAVE_JWT_CPP OFF)
    endif()
endif()

add_library(qornix_auth INTERFACE)

target_include_directories(qornix_auth INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(qornix_auth INTERFACE
    Boost::json
    Boost::url
    ${OPENSSL_LIBRARIES}
)

if(HAVE_JWT_CPP)
    target_compile_definitions(qornix_auth INTERFACE HAVE_JWT_CPP)
    target_link_libraries(qornix_auth INTERFACE jwt-cpp::jwt-cpp)
endif()

# Build examples only when this is the top-level project
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_executable(console_auth_example example/console_auth_example.cpp)
    target_link_libraries(console_auth_example PRIVATE qornix_auth)

    add_executable(web_auth_example example/web_auth_example.cpp)
    target_link_libraries(web_auth_example PRIVATE qornix_auth)
endif()

install(DIRECTORY include/ DESTINATION include/qornix_auth)
