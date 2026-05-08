cmake_minimum_required(VERSION 3.10)
project(wiki_app)

set(CMAKE_CXX_STANDARD 17)

add_executable(wiki_app
        main.cpp
        ../../server/http_server.cpp
)

target_compile_definitions(wiki_app PRIVATE
        WIKI_EXAMPLE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(wiki_app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_auth/include
)

find_package(Boost REQUIRED COMPONENTS url json log)
find_package(OpenSSL REQUIRED)

if(NOT TARGET qornix_orm)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../qornix_orm ${CMAKE_CURRENT_BINARY_DIR}/qornix_orm)
endif()

# qornix_auth is already added by parent CMakeLists.txt when ENABLE_AUTH=ON

# JWT support (optional)
find_package(jwt-cpp CONFIG QUIET)
if(TARGET jwt-cpp::jwt-cpp)
        target_compile_definitions(wiki_app PRIVATE HAVE_JWT_CPP)
        target_link_libraries(wiki_app PRIVATE jwt-cpp::jwt-cpp)
endif()

target_link_libraries(wiki_app PRIVATE
        Boost::url
        Boost::json
        Boost::log
        qornix_orm
        qornix_auth
        OpenSSL::SSL
        OpenSSL::Crypto
)
