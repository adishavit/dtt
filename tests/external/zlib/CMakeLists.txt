# zlib: FetchContent + configure-only (generates zconf.h)
FetchContent_Declare(zlib_ext
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_SHALLOW    TRUE
)

FetchContent_GetProperties(zlib_ext)
if(NOT zlib_ext_POPULATED)
    FetchContent_Populate(zlib_ext)
    # Configure zlib to generate zconf.h (required by zlib.h)
    # We do NOT build the library — only headers are needed for -fsyntax-only
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -B "${zlib_ext_BINARY_DIR}"
            -S "${zlib_ext_SOURCE_DIR}"
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        WORKING_DIRECTORY ${zlib_ext_SOURCE_DIR}
        OUTPUT_QUIET ERROR_QUIET
    )
endif()

dtt_add_external_lib_tests(
    LIB_PREFIX   zlib
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/zlib.dtt
    INCLUDE_DIRS "${zlib_ext_SOURCE_DIR};${zlib_ext_BINARY_DIR}"
)
