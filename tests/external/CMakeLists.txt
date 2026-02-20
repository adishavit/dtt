# External library behavioral tests for DTT
#
# Verifies that .dtt annotation files correctly catch API misuse
# when compiled against real library headers.
#
# All tests use -fsyntax-only (no linking required).
# Libraries are fetched via FetchContent or use checked-in stub headers.
#
# Usage:
#   cmake -B build -DCMAKE_CXX_COMPILER=clang++-18 -DDTT_TEST_EXTERNAL_LIBS=ON
#   cmake --build build -j$(nproc)
#   ctest --test-dir build -L external-lib -j8 --output-on-failure
#   ctest --test-dir build -L dtt-validate -j8

include(FetchContent)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DttExternalTestHelpers.cmake)

message(STATUS "DTT: Configuring external library behavioral tests")

# =========================================================================
# Parse validation: verify every .dtt file parses without errors
# =========================================================================
file(GLOB DTT_EXTERNAL_FILES ${CMAKE_SOURCE_DIR}/annotations/external/*.dtt)
foreach(dtt_file ${DTT_EXTERNAL_FILES})
    get_filename_component(dtt_name ${dtt_file} NAME_WE)
    set(test_name "validate_${dtt_name}_dtt")

    if(WIN32)
        add_test(NAME ${test_name}
            COMMAND ${CLANGXX} ${PLUGIN_ARGS}
                -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${dtt_file}
                -std=c++20 -fsyntax-only
                ${CMAKE_CURRENT_SOURCE_DIR}/parse_validate_stub.cpp)
    else()
        add_test(NAME ${test_name}
            COMMAND ${CLANGXX} ${PLUGIN_ARGS}
                -fplugin-arg-dtt---dtt-annotations=${dtt_file}
                -std=c++20 -fsyntax-only
                ${CMAKE_CURRENT_SOURCE_DIR}/parse_validate_stub.cpp)
    endif()

    set_tests_properties(${test_name} PROPERTIES
        LABELS "dtt-validate"
        FAIL_REGULAR_EXPRESSION "DTT annotation parse error|DTT: cannot open annotation file")
endforeach()

# =========================================================================
# Phase 1: Easy (header-only or trivial FetchContent)
# =========================================================================
add_subdirectory(zlib)
add_subdirectory(cpp-httplib)

# =========================================================================
# Phase 2: Medium libraries (FetchContent + raw headers)
# =========================================================================
add_subdirectory(libarchive)
add_subdirectory(libuv)
add_subdirectory(libusb)
add_subdirectory(s2n-tls)

# =========================================================================
# Phase 3: Placeholder subdirectories (tests to be added)
# =========================================================================
# add_subdirectory(libssh2)
# add_subdirectory(libpng)
# add_subdirectory(libdbus)
# add_subdirectory(ffmpeg)
# add_subdirectory(libwebrtc)
