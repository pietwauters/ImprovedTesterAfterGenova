cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ImprovedTester)

# Set version header path
set(VERSION_HEADER_PATH "${CMAKE_BINARY_DIR}/version.h")

# Create a script to generate version header
set(VERSION_SCRIPT_PATH "${CMAKE_BINARY_DIR}/generate_version.cmake")
file(WRITE ${VERSION_SCRIPT_PATH} "
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Fallback if git fails
if(NOT GIT_VERSION)
    set(GIT_VERSION \"unknown\")
endif()

# Generate version header
file(WRITE \"${VERSION_HEADER_PATH}\" \"#pragma once\\n#define APP_VERSION \\\"$\{GIT_VERSION\}\\\"\\n\")
")

# Custom target that always runs to generate version header
add_custom_target(generate_version ALL
    COMMAND ${CMAKE_COMMAND} -P ${VERSION_SCRIPT_PATH}
    BYPRODUCTS ${VERSION_HEADER_PATH}
    COMMENT "Generating version header"
    VERBATIM
)

# Make sure main component depends on version generation
# This will be handled in the main component's CMakeLists.txt
include_directories(${CMAKE_BINARY_DIR})