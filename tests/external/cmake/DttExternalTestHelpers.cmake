# DttExternalTestHelpers.cmake
#
# CMake helper functions for DTT external library behavioral tests.
# Provides auto-discovery of *_pass.cpp / *_fail.cpp / *_xfail.cpp
# and registers them as CTest tests with the correct plugin arguments.

# dtt_add_external_test()
#
# Registers a single DTT behavioral test for an external library.
# Uses -isystem for library headers to suppress warnings.
#
function(dtt_add_external_test)
    cmake_parse_arguments(PARSE_ARGV 0 ARG
        ""
        "NAME;SOURCE;DTT_FILE;WILL_FAIL"
        "INCLUDE_DIRS;EXTRA_FLAGS"
    )

    # -isystem flags for each include dir
    set(_include_flags "")
    foreach(_dir IN LISTS ARG_INCLUDE_DIRS)
        list(APPEND _include_flags "-isystem" "${_dir}")
    endforeach()

    # Platform-specific annotation loading flag
    if(WIN32)
        set(_annot_flag
            -Xclang -plugin-arg-dtt
            -Xclang --dtt-annotations=${ARG_DTT_FILE})
    else()
        set(_annot_flag
            -fplugin-arg-dtt---dtt-annotations=${ARG_DTT_FILE})
    endif()

    # Fail tests use enforce mode; pass tests use default mode
    if(ARG_WILL_FAIL)
        set(_plugin_args ${PLUGIN_ARGS_ENFORCE})
    else()
        set(_plugin_args ${PLUGIN_ARGS})
    endif()

    add_test(NAME ${ARG_NAME}
        COMMAND ${CLANGXX}
            ${_plugin_args}
            ${_annot_flag}
            ${_include_flags}
            ${ARG_EXTRA_FLAGS}
            -std=c++20 -fsyntax-only
            ${ARG_SOURCE}
    )

    if(ARG_WILL_FAIL)
        set_tests_properties(${ARG_NAME} PROPERTIES WILL_FAIL TRUE)
    endif()

    set_tests_properties(${ARG_NAME} PROPERTIES LABELS "external-lib")
endfunction()


# dtt_add_external_lib_tests()
#
# Auto-discovers *_pass.cpp, *_fail.cpp, and *_xfail.cpp in the current
# source directory and registers them as DTT external tests.
#
function(dtt_add_external_lib_tests)
    cmake_parse_arguments(PARSE_ARGV 0 ARG
        ""
        "LIB_PREFIX;DTT_FILE"
        "INCLUDE_DIRS;EXTRA_FLAGS"
    )

    # Pass tests
    file(GLOB _pass_tests "${CMAKE_CURRENT_SOURCE_DIR}/*_pass.cpp")
    foreach(_f IN LISTS _pass_tests)
        get_filename_component(_name ${_f} NAME_WE)
        dtt_add_external_test(
            NAME "ext_${_name}"
            SOURCE ${_f}
            DTT_FILE ${ARG_DTT_FILE}
            INCLUDE_DIRS ${ARG_INCLUDE_DIRS}
            WILL_FAIL FALSE
            EXTRA_FLAGS ${ARG_EXTRA_FLAGS}
        )
    endforeach()

    # Fail tests
    file(GLOB _fail_tests "${CMAKE_CURRENT_SOURCE_DIR}/*_fail.cpp")
    foreach(_f IN LISTS _fail_tests)
        get_filename_component(_name ${_f} NAME_WE)
        dtt_add_external_test(
            NAME "ext_${_name}"
            SOURCE ${_f}
            DTT_FILE ${ARG_DTT_FILE}
            INCLUDE_DIRS ${ARG_INCLUDE_DIRS}
            WILL_FAIL TRUE
            EXTRA_FLAGS ${ARG_EXTRA_FLAGS}
        )
    endforeach()

    # Xfail tests (known annotation gaps)
    file(GLOB _xfail_tests "${CMAKE_CURRENT_SOURCE_DIR}/*_xfail*.cpp")
    foreach(_f IN LISTS _xfail_tests)
        get_filename_component(_name ${_f} NAME_WE)
        dtt_add_external_test(
            NAME "ext_${_name}"
            SOURCE ${_f}
            DTT_FILE ${ARG_DTT_FILE}
            INCLUDE_DIRS ${ARG_INCLUDE_DIRS}
            WILL_FAIL TRUE
            EXTRA_FLAGS ${ARG_EXTRA_FLAGS}
        )
        set_tests_properties("ext_${_name}" PROPERTIES
            LABELS "external-lib;xfail")
    endforeach()
endfunction()
