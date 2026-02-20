# libssh2: Public headers in include/ are self-contained
FetchContent_Declare(libssh2_ext
    GIT_REPOSITORY https://github.com/libssh2/libssh2.git
    GIT_TAG        libssh2-1.11.1
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(libssh2_ext)

dtt_add_external_lib_tests(
    LIB_PREFIX   libssh2
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/libssh2.dtt
    INCLUDE_DIRS "${libssh2_ext_SOURCE_DIR}/include"
)
