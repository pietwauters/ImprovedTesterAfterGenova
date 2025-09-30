
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

file(WRITE ${HEADER_PATH} "#pragma once\n#define APP_VERSION \"${GIT_VERSION}\"\n")
