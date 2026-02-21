# cpp-httplib: Header-only library, trivial FetchContent
FetchContent_Declare(cpp_httplib_ext
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(cpp_httplib_ext)

dtt_add_external_lib_tests(
    LIB_PREFIX   httplib
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/cpp-httplib.dtt
    INCLUDE_DIRS "${cpp_httplib_ext_SOURCE_DIR}"
)
