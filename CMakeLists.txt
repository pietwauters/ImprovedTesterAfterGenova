cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ImprovedTester)
# Get git version string
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Generate version header
set(VERSION_HEADER_PATH "${CMAKE_BINARY_DIR}/version.h")
file(WRITE ${VERSION_HEADER_PATH} "#pragma once\n#define APP_VERSION \"${GIT_VERSION}\"\n")

# Make sure main target depends on this header
include_directories(${CMAKE_BINARY_DIR})