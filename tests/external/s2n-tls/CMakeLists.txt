# s2n-tls: Public API header api/s2n.h is self-contained
FetchContent_Declare(s2n_tls_ext
    GIT_REPOSITORY https://github.com/aws/s2n-tls.git
    GIT_TAG        v1.5.11
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(s2n_tls_ext)

dtt_add_external_lib_tests(
    LIB_PREFIX   s2n
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/s2n_tls.dtt
    INCLUDE_DIRS "${s2n_tls_ext_SOURCE_DIR}/api"
)
