# route_extensions/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(route_extensions)

set(CMAKE_CXX_STANDARD 17)

# Find all extension subdirectories
file(GLOB EXTENSION_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(DIR ${EXTENSION_DIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${DIR} AND
            EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/handler.cpp)
        message(STATUS "Building extension: ${DIR}")

        # Create a shared library for each extension
        add_library(${DIR}_route_extension SHARED
            ${DIR}/handler.cpp
        )

        # Set library properties
        set_target_properties(${DIR}_route_extension PROPERTIES
            PREFIX ""
            SUFFIX ".so"
        )

        # Link required libraries
        target_link_libraries(${DIR}_route_extension ${Boost_LIBRARIES})

        # Set include directories
        target_include_directories(${DIR}_route_extension PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../server
        )
    endif()
endforeach()
