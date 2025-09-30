# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources})

# Add version header as a dependency
set(VERSION_HEADER "${CMAKE_BINARY_DIR}/version.h")
add_custom_target(version_header DEPENDS ${VERSION_HEADER})
add_dependencies(${COMPONENT_LIB} version_header)
