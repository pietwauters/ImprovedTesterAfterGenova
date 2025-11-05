execute_process(

    COMMAND git describe --tags --always --dirtyexecute_process(

    WORKING_DIRECTORY ${SOURCE_DIR}    COMMAND git describe --tags --always --dirty

    OUTPUT_VARIABLE GIT_VERSION    WORKING_DIRECTORY ${SOURCE_DIR}

    OUTPUT_STRIP_TRAILING_WHITESPACE    OUTPUT_VARIABLE GIT_VERSION

    ERROR_QUIET    OUTPUT_STRIP_TRAILING_WHITESPACE

)    ERROR_QUIET

)

if(NOT GIT_VERSION)

    set(GIT_VERSION "unknown")if(NOT GIT_VERSION)

endif()    set(GIT_VERSION "unknown")

endif()

file(WRITE ${VERSION_HEADER_PATH} "#pragma once\n#define APP_VERSION \"${GIT_VERSION}\"\n")
file(WRITE ${HEADER_PATH} "#pragma once\n#define APP_VERSION \"${GIT_VERSION}\"\n")
