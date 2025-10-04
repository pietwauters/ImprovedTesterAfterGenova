cmake_minimum_required(VERSION 3.16.0)

# Generate version header BEFORE project() call to ensure it exists
set(VERSION_HEADER_PATH "${CMAKE_CURRENT_BINARY_DIR}/version.h")

# Get git version string - execute immediately at configuration time
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Fallback if git fails
if(NOT GIT_VERSION)
    set(GIT_VERSION "unknown")
endif()

# Always write version header at configuration time
file(WRITE ${VERSION_HEADER_PATH} "#pragma once\n#define APP_VERSION \"${GIT_VERSION}\"\n")

# Create the update script
file(WRITE "${CMAKE_SOURCE_DIR}/update_version.cmake" "
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY \${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT GIT_VERSION)
    set(GIT_VERSION \"unknown\")
endif()

file(WRITE \${HEADER_PATH} \"#pragma once\\n#define APP_VERSION \\\"\${GIT_VERSION}\\\"\\n\")
")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ImprovedTester)

# Make sure header is in include path
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Create a target that always runs to update version
add_custom_target(update_version_always ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DHEADER_PATH=${VERSION_HEADER_PATH} -P ${CMAKE_SOURCE_DIR}/update_version.cmake
    COMMENT "Updating version header from git"
    VERBATIM
)