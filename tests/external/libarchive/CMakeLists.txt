# libarchive: Public headers (archive.h, archive_entry.h) are self-contained
FetchContent_Declare(libarchive_ext
    GIT_REPOSITORY https://github.com/libarchive/libarchive.git
    GIT_TAG        v3.7.7
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(libarchive_ext)

dtt_add_external_lib_tests(
    LIB_PREFIX   libarchive
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/libarchive.dtt
    INCLUDE_DIRS "${libarchive_ext_SOURCE_DIR}/libarchive"
)
