# libusb: Header in libusb/ is self-contained
FetchContent_Declare(libusb_ext
    GIT_REPOSITORY https://github.com/libusb/libusb.git
    GIT_TAG        v1.0.27
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(libusb_ext)

dtt_add_external_lib_tests(
    LIB_PREFIX   libusb
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/libusb.dtt
    INCLUDE_DIRS "${libusb_ext_SOURCE_DIR}/libusb"
)
