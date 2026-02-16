cmake_minimum_required(VERSION 3.16)
project(DTTPlugin)

# Find LLVM and Clang
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# Find Z3 (for SMT solving)
find_package(Z3 CONFIG QUIET)
if(NOT Z3_FOUND)
    # Fallback for systems without Z3Config.cmake
    find_path(Z3_INCLUDE_DIRS z3++.h
        HINTS
            /usr/include
            /usr/local/include
            $ENV{HOMEBREW_PREFIX}/include
            /opt/homebrew/include
            $ENV{Z3_ROOT}/include
            "C:/Program Files/Z3/include"
    )
    find_library(Z3_LIBRARIES z3
        HINTS
            /usr/lib
            /usr/local/lib
            $ENV{HOMEBREW_PREFIX}/lib
            /opt/homebrew/lib
            $ENV{Z3_ROOT}/lib
            $ENV{Z3_ROOT}/bin
            "C:/Program Files/Z3/lib"
            "C:/Program Files/Z3/bin"
    )
    if(Z3_INCLUDE_DIRS AND Z3_LIBRARIES)
        message(STATUS "Found Z3: ${Z3_LIBRARIES}")
    else()
        message(FATAL_ERROR "Z3 not found. Install with: apt install z3 (Linux), brew install z3 (macOS), or choco install z3 (Windows)")
    endif()
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Get LLVM flags
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Include directories
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})
if(Z3_INCLUDE_DIRS)
    include_directories(${Z3_INCLUDE_DIRS})
endif()

# Build the plugin as a shared library
add_library(DTTPlugin SHARED
    src/DTTPlugin.cpp
    src/DTTStdLib.cpp
    src/DTTCLib.cpp
    src/DTTWinLib.cpp
    src/DTTAnnotationFile.cpp
)

# Platform-specific linker flags
# Plugins are loaded into clang, so we don't link against LLVM/Clang libs
if(APPLE)
    set_target_properties(DTTPlugin PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(UNIX)
    set_target_properties(DTTPlugin PROPERTIES
        LINK_FLAGS "-Wl,--unresolved-symbols=ignore-all"
    )
elseif(WIN32)
    # On Windows, we need to link against LLVM/Clang libraries
    target_link_libraries(DTTPlugin PRIVATE
        clangAST
        clangBasic
        clangFrontend
        clangSema
        LLVMSupport
    )
endif()

# Link Z3 library (for direct C++ API instead of shell-out)
if(Z3_LIBRARIES)
    target_link_libraries(DTTPlugin PRIVATE ${Z3_LIBRARIES})
endif()

# Disable RTTI to match LLVM/Clang build
if(NOT LLVM_ENABLE_RTTI)
    if(MSVC)
        target_compile_options(DTTPlugin PRIVATE /GR-)
    else()
        target_compile_options(DTTPlugin PRIVATE -fno-rtti)
    endif()
endif()

# Installation
install(TARGETS DTTPlugin
    LIBRARY DESTINATION lib
)

# Export compile_commands.json for IDE support (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Testing (auto-discover by naming convention)
enable_testing()
set(CLANGXX ${CMAKE_CXX_COMPILER})

# Custom target: parallel build + parallel test run
# Usage: cmake --build build --target test-all
add_custom_target(test-all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --parallel
    COMMAND ${CMAKE_CTEST_COMMAND} -j 8 --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building and running all tests in parallel"
    USES_TERMINAL)

# Detect available standard libraries for clang
# We always have libstdc++ available, check if libc++ is also available
set(DTT_STDLIB_LIBSTDCXX ON)
set(DTT_STDLIB_LIBCXX OFF)

# Option to auto-install libc++ if missing
option(DTT_INSTALL_LIBCXX "Automatically install libc++ if missing" OFF)

# Check for libc++ availability
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Try to compile a simple program with libc++
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-stdlib=libc++")
    check_cxx_source_compiles("
        #include <vector>
        int main() { std::vector<int> v; return 0; }
    " HAVE_LIBCXX)
    unset(CMAKE_REQUIRED_FLAGS)

    if(HAVE_LIBCXX)
        set(DTT_STDLIB_LIBCXX ON)
        message(STATUS "libc++ detected - tests will run with both libstdc++ and libc++")
    else()
        # Try to auto-install libc++ if requested
        if(DTT_INSTALL_LIBCXX)
            # Detect clang version for matching libc++ package
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} --version
                OUTPUT_VARIABLE CLANG_VERSION_OUTPUT
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            string(REGEX MATCH "clang version ([0-9]+)" _ "${CLANG_VERSION_OUTPUT}")
            set(CLANG_MAJOR_VERSION ${CMAKE_MATCH_1})

            message(STATUS "Attempting to install libc++-${CLANG_MAJOR_VERSION}-dev...")

            # Install libc++ packages
            execute_process(
                COMMAND sudo apt-get install -y libc++-${CLANG_MAJOR_VERSION}-dev libc++abi-${CLANG_MAJOR_VERSION}-dev
                RESULT_VARIABLE INSTALL_RESULT
                OUTPUT_VARIABLE INSTALL_OUTPUT
                ERROR_VARIABLE INSTALL_ERROR
            )

            if(INSTALL_RESULT EQUAL 0)
                message(STATUS "libc++ installed successfully")
                set(DTT_STDLIB_LIBCXX ON)
            else()
                message(WARNING "Failed to install libc++: ${INSTALL_ERROR}")
                message(STATUS "Install manually: sudo apt-get install libc++-${CLANG_MAJOR_VERSION}-dev libc++abi-${CLANG_MAJOR_VERSION}-dev")
            endif()
        else()
            message(STATUS "libc++ not available - tests will run with libstdc++ only")
            message(STATUS "To enable libc++ tests, either:")
            message(STATUS "  1. Install manually: sudo apt-get install libc++-18-dev libc++abi-18-dev")
            message(STATUS "  2. Re-run cmake with: -DDTT_INSTALL_LIBCXX=ON")
        endif()
    endif()
endif()

# Platform-specific plugin loading
# On Windows with clang-cl, use -Xclang -load -Xclang <plugin>
# On Unix, use -fplugin=<plugin>
if(WIN32)
    set(PLUGIN_ARGS -Xclang -load -Xclang $<TARGET_FILE:DTTPlugin> -Xclang -add-plugin -Xclang dtt)
    set(PLUGIN_ARGS_ENFORCE ${PLUGIN_ARGS} -Xclang -plugin-arg-dtt -Xclang --dtt-mode=enforce)
    set(PLUGIN_ARGS_QUIET ${PLUGIN_ARGS} -Xclang -plugin-arg-dtt -Xclang --dtt-mode=quiet)
    set(PLUGIN_ARGS_INFORM ${PLUGIN_ARGS} -Xclang -plugin-arg-dtt -Xclang --dtt-mode=inform)
else()
    set(PLUGIN_ARGS -fplugin=$<TARGET_FILE:DTTPlugin>)
    set(PLUGIN_ARGS_ENFORCE ${PLUGIN_ARGS} -fplugin-arg-dtt---dtt-mode=enforce)
    set(PLUGIN_ARGS_QUIET ${PLUGIN_ARGS} -fplugin-arg-dtt---dtt-mode=quiet)
    set(PLUGIN_ARGS_INFORM ${PLUGIN_ARGS} -fplugin-arg-dtt---dtt-mode=inform)
endif()

file(GLOB PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/*_pass.cpp)
file(GLOB FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/*_fail.cpp)
file(GLOB XFAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/*_xfail*.cpp)
file(GLOB XPASS_TESTS_EARLY ${CMAKE_SOURCE_DIR}/examples/*_xpass*.cpp)
file(GLOB ALL_TESTS ${CMAKE_SOURCE_DIR}/examples/*.cpp)

# Exclude xfail/xpass from pass/fail globs (e.g. *_xpass.cpp matches *_pass.cpp)
list(REMOVE_ITEM PASS_TESTS ${XPASS_TESTS_EARLY})
list(REMOVE_ITEM FAIL_TESTS ${XFAIL_TESTS})

# Exclude annotation file tests from auto-glob (they need custom plugin args)
list(REMOVE_ITEM PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/1710_annotation_file_pass.cpp)
list(REMOVE_ITEM FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/1710_annotation_file_fail.cpp)

# Build list of stdlib configurations to test
set(DTT_STDLIB_CONFIGS "libstdc++")
if(DTT_STDLIB_LIBCXX)
    list(APPEND DTT_STDLIB_CONFIGS "libc++")
endif()

# Helper function to add tests for all stdlib configurations
function(dtt_add_test name plugin_args will_fail test_file)
    foreach(stdlib ${DTT_STDLIB_CONFIGS})
        if(stdlib STREQUAL "libstdc++")
            set(test_name "${name}")
            set(stdlib_flag "")
        else()
            set(test_name "${name}_libcxx")
            set(stdlib_flag "-stdlib=libc++")
        endif()

        add_test(NAME ${test_name}
            COMMAND ${CLANGXX} ${plugin_args} ${stdlib_flag} -std=c++20 -fsyntax-only ${test_file})

        if(will_fail)
            set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
        endif()

        # Add stdlib label for filtering
        if(NOT stdlib STREQUAL "libstdc++")
            set_tests_properties(${test_name} PROPERTIES LABELS "${stdlib}")
        endif()
    endforeach()
endfunction()

foreach(f ${PASS_TESTS})
    get_filename_component(name ${f} NAME_WE)
    dtt_add_test(${name} "${PLUGIN_ARGS}" FALSE ${f})
endforeach()

# _fail tests run in enforce mode so Category C (lost tracking) produces errors
foreach(f ${FAIL_TESTS})
    get_filename_component(name ${f} NAME_WE)
    dtt_add_test(${name} "${PLUGIN_ARGS_ENFORCE}" TRUE ${f})
endforeach()

# XFAIL tests: features that SHOULD work but are NOT YET IMPLEMENTED
# These tests document desired behavior that isn't implemented yet.
# Each test produces a DTT error for code that SHOULD compile cleanly.
# When the feature is implemented, the test will "unexpectedly pass",
# alerting us to update the test status.
#
# Test behavior:
# - Unimplemented: DTT error → compilation fails → WILL_FAIL satisfied → test PASSES
# - Implemented:   No error  → compilation succeeds → WILL_FAIL violated → test FAILS
#
# NOTE: xfail tests run in ENFORCE mode so Category C (lost tracking) produces
# errors, maintaining the expected behavior of these regression tests.
#
# To run xfail tests: ctest -L xfail
# To exclude in CI:   ctest -LE xfail

# Extended helper function for tests with additional labels
function(dtt_add_test_with_labels name plugin_args will_fail test_file extra_labels)
    foreach(stdlib ${DTT_STDLIB_CONFIGS})
        if(stdlib STREQUAL "libstdc++")
            set(test_name "${name}")
            set(stdlib_flag "")
            set(stdlib_label "")
        else()
            set(test_name "${name}_libcxx")
            set(stdlib_flag "-stdlib=libc++")
            set(stdlib_label "${stdlib}")
        endif()

        add_test(NAME ${test_name}
            COMMAND ${CLANGXX} ${plugin_args} ${stdlib_flag} -std=c++20 -fsyntax-only ${test_file})

        if(will_fail)
            set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
        endif()

        # Combine extra labels with stdlib label
        set(all_labels "${extra_labels}")
        if(stdlib_label)
            list(APPEND all_labels "${stdlib_label}")
        endif()
        if(all_labels)
            set_tests_properties(${test_name} PROPERTIES LABELS "${all_labels}")
        endif()
    endforeach()
endfunction()

file(GLOB XFAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/*_xfail*.cpp)
foreach(f ${XFAIL_TESTS})
    get_filename_component(name ${f} NAME_WE)
    dtt_add_test_with_labels(${name} "${PLUGIN_ARGS_ENFORCE}" TRUE ${f} "xfail;future_feature")
endforeach()

# XPASS tests: Document false positives - safe code that DTT incorrectly rejects
#
# The mirror of xfail: code is correct but DTT flags it as an error.
# Examples: inline asm output operands, sizeof on unevaluated operands.
#
# Test behavior:
# - False positive exists: DTT error -> compilation fails -> WILL_FAIL satisfied -> test PASSES
# - False positive fixed:  No error  -> compilation succeeds -> WILL_FAIL violated -> test FAILS
#
# When a false positive is fixed, rename *_xpass.cpp to *_pass.cpp
#
# To run xpass tests: ctest -L xpass
# To exclude in CI:   ctest -LE xpass
file(GLOB XPASS_TESTS ${CMAKE_SOURCE_DIR}/examples/*_xpass*.cpp)
foreach(f ${XPASS_TESTS})
    get_filename_component(name ${f} NAME_WE)
    dtt_add_test_with_labels(${name} "${PLUGIN_ARGS_ENFORCE}" TRUE ${f} "xpass;false_positive")
endforeach()

# SILENT tests: Document cases where DTT is completely silent (false negatives)
#
# These tests contain code with potential null dereferences that DTT doesn't detect:
# - Smart pointers (class types, not pointer types - DTT ignores them)
# - Pointer-to-container aliasing (pc->ptr = null doesn't update c.ptr tracking)
#
# No WILL_FAIL - compilation succeeds because DTT produces no errors.
# These tests pass to confirm DTT's current (limited) behavior is stable.
#
# When DTT is enhanced to detect these cases, convert to *_fail.cpp
file(GLOB SILENT_TESTS ${CMAKE_SOURCE_DIR}/examples/*_silent.cpp)
foreach(f ${SILENT_TESTS})
    get_filename_component(name ${f} NAME_WE)
    dtt_add_test_with_labels(${name} "${PLUGIN_ARGS}" FALSE ${f} "silent;not_tracked")
endforeach()

# =========================================================================
# External .dtt annotation file tests
# These tests use --dtt-annotations= to load a custom annotation file
# =========================================================================
if(WIN32)
    set(DTT_ANNOT_ARGS ${PLUGIN_ARGS} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_custom.dtt)
    set(DTT_ANNOT_ARGS_ENFORCE ${PLUGIN_ARGS_ENFORCE} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_custom.dtt)
else()
    set(DTT_ANNOT_ARGS ${PLUGIN_ARGS} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_custom.dtt)
    set(DTT_ANNOT_ARGS_ENFORCE ${PLUGIN_ARGS_ENFORCE} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_custom.dtt)
endif()

add_test(NAME 1710_annotation_file_pass
    COMMAND ${CLANGXX} ${DTT_ANNOT_ARGS} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1710_annotation_file_pass.cpp)

add_test(NAME 1710_annotation_file_fail
    COMMAND ${CLANGXX} ${DTT_ANNOT_ARGS_ENFORCE} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1710_annotation_file_fail.cpp)
set_tests_properties(1710_annotation_file_fail PROPERTIES WILL_FAIL TRUE)

# Warn about non-conforming test files (not *_pass.cpp, *_fail.cpp, *_xfail.cpp, or *_silent.cpp)
foreach(f ${ALL_TESTS})
    get_filename_component(name ${f} NAME)
    if(NOT name MATCHES "_pass\\.cpp$" AND NOT name MATCHES "_fail\\.cpp$" AND NOT name MATCHES "_xfail" AND NOT name MATCHES "_xpass" AND NOT name MATCHES "_silent\\.cpp$")
        message(WARNING "Test file '${name}' doesn't follow naming convention (*_pass.cpp, *_fail.cpp, *_xfail.cpp, *_xpass.cpp, or *_silent.cpp) - assuming PASS")
        get_filename_component(test_name ${f} NAME_WE)
        dtt_add_test(${test_name} "${PLUGIN_ARGS}" FALSE ${f})
    endif()
endforeach()
