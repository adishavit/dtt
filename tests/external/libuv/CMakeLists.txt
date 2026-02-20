# libuv: Public headers in include/ are self-contained
FetchContent_Declare(libuv_ext
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG        v1.49.2
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(libuv_ext)

dtt_add_external_lib_tests(
    LIB_PREFIX   libuv
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/libuv.dtt
    INCLUDE_DIRS "${libuv_ext_SOURCE_DIR}/include"
)
