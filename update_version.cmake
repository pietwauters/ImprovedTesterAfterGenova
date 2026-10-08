# Writes ${VERSION_HEADER_PATH} with APP_VERSION from `git describe` (run by the
# update_version_always target in CMakeLists.txt; PlatformIO builds also write
# it from extra_script.py)
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT GIT_VERSION)
    set(GIT_VERSION "unknown")
endif()

set(CONTENT "#pragma once\n#define APP_VERSION \"${GIT_VERSION}\"\n")
if(EXISTS ${VERSION_HEADER_PATH})
    file(READ ${VERSION_HEADER_PATH} OLD_CONTENT)
endif()
if(NOT "${CONTENT}" STREQUAL "${OLD_CONTENT}")
    file(WRITE ${VERSION_HEADER_PATH} "${CONTENT}")
endif()
