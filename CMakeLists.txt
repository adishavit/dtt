cmake_minimum_required(VERSION 3.16)
project(DTTPlugin)

# Import matching targets before Clang can rediscover LLVM. Importing another
# LLVM version first leaves its targets alive even if Clang replaces LLVM_DIR.
find_package(LLVM 18.1 REQUIRED CONFIG)
file(TO_CMAKE_PATH "${LLVM_DIR}/../clang" DTT_CLANG_CMAKE_HINT)
find_package(Clang ${LLVM_PACKAGE_VERSION} EXACT REQUIRED CONFIG
    HINTS "${DTT_CLANG_CMAKE_HINT}")

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
    src/DTTAnnotationFile.cpp
    src/DTTSarif.cpp
)

# Keep standalone plugin downloads self-contained. Reconfigure when contracts change.
foreach(builtin std libc win32)
    set(annotation "${CMAKE_CURRENT_SOURCE_DIR}/annotations/${builtin}.dtt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${annotation}")
    file(READ "${annotation}" DTT_${builtin})
endforeach()
configure_file(src/DTTBuiltinAnnotations.inc.in generated/DTTBuiltinAnnotations.inc @ONLY)
target_include_directories(DTTPlugin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Platform-specific linker flags
# Unix plugins resolve Clang symbols from their host at load time.
if(APPLE)
    set_target_properties(DTTPlugin PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(UNIX)
    set_target_properties(DTTPlugin PROPERTIES
        LINK_FLAGS "-Wl,--unresolved-symbols=ignore-all"
    )
elseif(WIN32)
    set(DTT_CLANG_IMPORT_LIBRARY "" CACHE FILEPATH
        "Import library for the matching clang.exe built with LLVM_EXPORT_SYMBOLS_FOR_PLUGINS=ON")
    if(NOT EXISTS "${DTT_CLANG_IMPORT_LIBRARY}")
        message(FATAL_ERROR
            "Set DTT_CLANG_IMPORT_LIBRARY to the matching clang.exe import library")
    endif()
    # Import the host registry first; retain static fallbacks as LLVM's plugins do.
    target_link_libraries(DTTPlugin PRIVATE
        "${DTT_CLANG_IMPORT_LIBRARY}"
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

# Linux runtime bundles resolve Z3 beside the installed plugin. Other platforms
# retain their existing source-install behavior until native packaging is verified.
option(DTT_BUNDLE_Z3 "Bundle the Z3 runtime in Linux installations" OFF)
if(DTT_BUNDLE_Z3)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "DTT_BUNDLE_Z3 currently supports Linux only")
    endif()
    set_target_properties(DTTPlugin PROPERTIES INSTALL_RPATH "$ORIGIN")
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/BundleZ3-$<CONFIG>.cmake"
         INPUT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundleZ3.cmake.in")
endif()

# Installation
install(TARGETS DTTPlugin
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
if(DTT_BUNDLE_Z3)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/BundleZ3-$<CONFIG>.cmake")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/z3/LICENSE.txt"
            DESTINATION share/dtt/licenses/z3)
endif()
install(FILES annotations/std.dtt annotations/libc.dtt annotations/win32.dtt
    DESTINATION share/dtt/annotations)
install(DIRECTORY annotations/external DESTINATION share/dtt/annotations
    FILES_MATCHING PATTERN "*.dtt")

# Export compile_commands.json for IDE support (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Testing (auto-discover by naming convention)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DTT_ERROR_RUNNER "${CMAKE_SOURCE_DIR}/tests/expect_dtt_error.py")
set(DTT_DIAGNOSTIC_FLAGS -ferror-limit=0 -fno-color-diagnostics)
set(CLANGXX "${CMAKE_CXX_COMPILER}" CACHE FILEPATH "Clang driver used to load and test DTT")

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
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT WIN32)
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
list(REMOVE_ITEM PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/1741_requires_value_pass.cpp)
list(REMOVE_ITEM FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/1741_requires_value_fail.cpp)
list(REMOVE_ITEM PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/1711_logical_constraint_pass.cpp)
list(REMOVE_ITEM FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/1711_logical_constraint_fail.cpp)
list(REMOVE_ITEM PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/1712_enum_constraint_pass.cpp)
list(REMOVE_ITEM FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/1712_enum_constraint_fail.cpp)
list(REMOVE_ITEM PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/1100_addr_of_nonnull_pass.cpp)
list(REMOVE_ITEM FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/1101_arity_mismatch_fail.cpp)
list(REMOVE_ITEM PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/1102_dtt_frame_semantics_pass.cpp)
list(REMOVE_ITEM PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/1713_when_guard_pass.cpp)
list(REMOVE_ITEM FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/1713_when_guard_fail.cpp)
list(REMOVE_ITEM PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/1714_when_guard_general_pass.cpp)
list(REMOVE_ITEM FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/1714_when_guard_general_fail.cpp)

# Pass tests that assert a proof run in enforce mode, where a check DTT
# cannot prove is an error instead of a warning.
set(ENFORCE_PASS_TESTS
    ${CMAKE_SOURCE_DIR}/examples/1740_c_division_semantics_pass.cpp
    ${CMAKE_SOURCE_DIR}/examples/1742_one_sided_branch_write_pass.cpp)
list(REMOVE_ITEM PASS_TESTS ${ENFORCE_PASS_TESTS})

# Build list of stdlib configurations to test
set(DTT_STDLIB_CONFIGS "libstdc++")
if(DTT_STDLIB_LIBCXX)
    list(APPEND DTT_STDLIB_CONFIGS "libc++")
endif()

# Fixtures requiring APIs absent from the Windows UCRT. Keep portable scenarios
# in separate fixtures so Windows still checks their analyzer behavior.
set(DTT_POSIX_TESTS
    1330_posix_dir_ops_pass
    1330_posix_dir_ops_fail
    1400_pthread_lifecycle_pass
    1400_pthread_lifecycle_xfail
    1502_realpath_conditional_alloc_xfail
    1503a_getenv_setenv_invalidation_xfail
    1504_readdir_internal_ptr_xfail
    1505_pthread_lock_state_xfail
    1620_generic_uaf_fail
    1703a_popen_transfer_return_pass
    1703b_ownership_transfer_fd_xpass
    705a_aligned_alloc_dealloc_fail
    838a_aligned_alloc_pass)
set(DTT_LINUX_TESTS
    1360_posix_resource_pass
    1360_posix_resource_fail)

function(dtt_test_supported name result)
    if(WIN32 AND "${name}" IN_LIST DTT_POSIX_TESTS)
        message(STATUS "Skipping ${name}: fixture requires APIs unavailable on Windows")
        set(${result} FALSE PARENT_SCOPE)
    elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND "${name}" IN_LIST DTT_LINUX_TESTS)
        message(STATUS "Skipping ${name}: fixture requires Linux epoll")
        set(${result} FALSE PARENT_SCOPE)
    else()
        set(${result} TRUE PARENT_SCOPE)
    endif()
endfunction()

# Helper function to add tests for all stdlib configurations
function(dtt_add_test name plugin_args will_fail test_file)
    dtt_test_supported("${name}" supported)
    if(NOT supported)
        return()
    endif()
    foreach(stdlib ${DTT_STDLIB_CONFIGS})
        if(stdlib STREQUAL "libstdc++")
            set(test_name "${name}")
            set(stdlib_flag "")
        else()
            set(test_name "${name}_libcxx")
            set(stdlib_flag "-stdlib=libc++")
        endif()

        if(will_fail)
            add_test(NAME ${test_name}
                COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER} ${test_file} --
                    ${CLANGXX} ${plugin_args} ${stdlib_flag} ${DTT_DIAGNOSTIC_FLAGS}
                    -std=c++20 -fsyntax-only ${test_file})
        else()
            add_test(NAME ${test_name}
                COMMAND ${CLANGXX} ${plugin_args} ${stdlib_flag} -std=c++20 -fsyntax-only ${test_file})
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

foreach(f ${ENFORCE_PASS_TESTS})
    get_filename_component(name ${f} NAME_WE)
    dtt_add_test(${name} "${PLUGIN_ARGS_ENFORCE}" FALSE ${f})
endforeach()

# _fail tests run in enforce mode so Category C (lost tracking) produces errors
foreach(f ${FAIL_TESTS})
    get_filename_component(name ${f} NAME_WE)
    dtt_add_test(${name} "${PLUGIN_ARGS_ENFORCE}" TRUE ${f})
endforeach()

# XFAIL tests document desired detection that is not implemented yet.
# Compilation succeeds while the gap exists, so WILL_FAIL reports a failure.
# When DTT catches the intended violation, verify it and rename to *_fail.cpp.
# Known-gap xfail/xpass tests retain exit-code semantics during incremental migration.
# To run xfail tests: ctest -L xfail; to exclude them: ctest -LE xfail.

# Extended helper function for tests with additional labels
function(dtt_add_test_with_labels name plugin_args will_fail test_file extra_labels)
    dtt_test_supported("${name}" supported)
    if(NOT supported)
        return()
    endif()
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

# SILENT fixtures record patterns intentionally not diagnosed by the current model.
# If detection is added, verify the intended diagnostic and reclassify the fixture.
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
    COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER} ${CMAKE_SOURCE_DIR}/examples/1710_annotation_file_fail.cpp --
        ${CLANGXX} ${DTT_ANNOT_ARGS_ENFORCE} -std=c++20 -fsyntax-only ${DTT_DIAGNOSTIC_FLAGS}
        ${CMAKE_SOURCE_DIR}/examples/1710_annotation_file_fail.cpp)

add_test(NAME 1741_requires_value_pass
    COMMAND ${CLANGXX} ${DTT_ANNOT_ARGS_ENFORCE} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1741_requires_value_pass.cpp)

add_test(NAME 1741_requires_value_fail
    COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER} ${CMAKE_SOURCE_DIR}/examples/1741_requires_value_fail.cpp --
        ${CLANGXX} ${DTT_ANNOT_ARGS_ENFORCE} -std=c++20 -fsyntax-only ${DTT_DIAGNOSTIC_FLAGS}
        ${CMAKE_SOURCE_DIR}/examples/1741_requires_value_fail.cpp)

add_test(NAME 1100_addr_of_nonnull_pass
    COMMAND ${CLANGXX} ${DTT_ANNOT_ARGS_ENFORCE} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1100_addr_of_nonnull_pass.cpp)

add_test(NAME 1101_arity_mismatch_fail
    COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER} ${CMAKE_SOURCE_DIR}/examples/1101_arity_mismatch_fail.cpp --
        ${CLANGXX} ${DTT_ANNOT_ARGS_ENFORCE} -std=c++20 -fsyntax-only ${DTT_DIAGNOSTIC_FLAGS}
        ${CMAKE_SOURCE_DIR}/examples/1101_arity_mismatch_fail.cpp)

# Frame semantics test - .dtt-annotated functions should not invalidate args
if(WIN32)
    set(DTT_FRAME_ARGS ${PLUGIN_ARGS_ENFORCE} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_frame.dtt)
else()
    set(DTT_FRAME_ARGS ${PLUGIN_ARGS_ENFORCE} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_frame.dtt)
endif()
add_test(NAME 1102_dtt_frame_semantics_pass
    COMMAND ${CLANGXX} ${DTT_FRAME_ARGS} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1102_dtt_frame_semantics_pass.cpp)

# Logical operator tests (&&, ||) - need test_logical.dtt loaded via --dtt-annotations=
if(APPLE)
    set(DTT_LOGICAL_ARGS ${PLUGIN_ARGS} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_logical.dtt)
    set(DTT_LOGICAL_ARGS_ENFORCE ${PLUGIN_ARGS_ENFORCE} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_logical.dtt)
else()
    set(DTT_LOGICAL_ARGS ${PLUGIN_ARGS} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_logical.dtt)
    set(DTT_LOGICAL_ARGS_ENFORCE ${PLUGIN_ARGS_ENFORCE} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_logical.dtt)
endif()

add_test(NAME 1711_logical_constraint_pass
    COMMAND ${CLANGXX} ${DTT_LOGICAL_ARGS} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1711_logical_constraint_pass.cpp)

add_test(NAME 1711_logical_constraint_fail
    COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER} ${CMAKE_SOURCE_DIR}/examples/1711_logical_constraint_fail.cpp --
        ${CLANGXX} ${DTT_LOGICAL_ARGS_ENFORCE} -std=c++20 -fsyntax-only ${DTT_DIAGNOSTIC_FLAGS}
        ${CMAKE_SOURCE_DIR}/examples/1711_logical_constraint_fail.cpp)

# Named enumeration tests (Gap 5) - need test_enum.dtt loaded via --dtt-annotations=
if(APPLE)
    set(DTT_ENUM_ARGS ${PLUGIN_ARGS} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_enum.dtt)
    set(DTT_ENUM_ARGS_ENFORCE ${PLUGIN_ARGS_ENFORCE} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_enum.dtt)
else()
    set(DTT_ENUM_ARGS ${PLUGIN_ARGS} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_enum.dtt)
    set(DTT_ENUM_ARGS_ENFORCE ${PLUGIN_ARGS_ENFORCE} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_enum.dtt)
endif()

add_test(NAME 1712_enum_constraint_pass
    COMMAND ${CLANGXX} ${DTT_ENUM_ARGS_ENFORCE} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1712_enum_constraint_pass.cpp)

add_test(NAME 1712_enum_constraint_fail
    COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER} ${CMAKE_SOURCE_DIR}/examples/1712_enum_constraint_fail.cpp --
        ${CLANGXX} ${DTT_ENUM_ARGS_ENFORCE} -std=c++20 -fsyntax-only ${DTT_DIAGNOSTIC_FLAGS}
        ${CMAKE_SOURCE_DIR}/examples/1712_enum_constraint_fail.cpp)

# =========================================================================
# External library behavioral tests (real headers via FetchContent)
# Disabled by default; enable with -DDTT_TEST_EXTERNAL_LIBS=ON
# Run independently with: ctest -L external-lib -j8
# Parse validation:       ctest -L dtt-validate -j8
# =========================================================================
option(DTT_TEST_EXTERNAL_LIBS
    "Build external library DTT behavioral tests (requires internet for FetchContent)" OFF)
if(DTT_TEST_EXTERNAL_LIBS)
    add_subdirectory(tests/external)
endif()

# Conditional postcondition tests (when guards) - need test_when.dtt loaded via --dtt-annotations=
if(APPLE)
    set(DTT_WHEN_ARGS ${PLUGIN_ARGS} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_when.dtt)
    set(DTT_WHEN_ARGS_ENFORCE ${PLUGIN_ARGS_ENFORCE} -Xclang -plugin-arg-dtt -Xclang --dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_when.dtt)
else()
    set(DTT_WHEN_ARGS ${PLUGIN_ARGS} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_when.dtt)
    set(DTT_WHEN_ARGS_ENFORCE ${PLUGIN_ARGS_ENFORCE} -fplugin-arg-dtt---dtt-annotations=${CMAKE_SOURCE_DIR}/annotations/test_when.dtt)
endif()

add_test(NAME 1713_when_guard_pass
    COMMAND ${CLANGXX} ${DTT_WHEN_ARGS_ENFORCE} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1713_when_guard_pass.cpp)

add_test(NAME 1713_when_guard_fail
    COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER} ${CMAKE_SOURCE_DIR}/examples/1713_when_guard_fail.cpp --
        ${CLANGXX} ${DTT_WHEN_ARGS_ENFORCE} -std=c++20 -fsyntax-only ${DTT_DIAGNOSTIC_FLAGS}
        ${CMAKE_SOURCE_DIR}/examples/1713_when_guard_fail.cpp)

add_test(NAME 1714_when_guard_general_pass
    COMMAND ${CLANGXX} ${DTT_WHEN_ARGS_ENFORCE} -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/1714_when_guard_general_pass.cpp)

add_test(NAME 1714_when_guard_general_fail
    COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER} ${CMAKE_SOURCE_DIR}/examples/1714_when_guard_general_fail.cpp --
        ${CLANGXX} ${DTT_WHEN_ARGS_ENFORCE} -std=c++20 -fsyntax-only ${DTT_DIAGNOSTIC_FLAGS}
        ${CMAKE_SOURCE_DIR}/examples/1714_when_guard_general_fail.cpp)
# Warn about non-conforming test files (not *_pass.cpp, *_fail.cpp, *_xfail.cpp, or *_silent.cpp)
foreach(f ${ALL_TESTS})
    get_filename_component(name ${f} NAME)
    if(NOT name MATCHES "_pass\\.cpp$" AND NOT name MATCHES "_fail\\.cpp$" AND NOT name MATCHES "_xfail" AND NOT name MATCHES "_xpass" AND NOT name MATCHES "_silent\\.cpp$")
        message(WARNING "Test file '${name}' doesn't follow naming convention (*_pass.cpp, *_fail.cpp, *_xfail.cpp, *_xpass.cpp, or *_silent.cpp) - assuming PASS")
        get_filename_component(test_name ${f} NAME_WE)
        dtt_add_test(${test_name} "${PLUGIN_ARGS}" FALSE ${f})
    endif()
endforeach()

# ====================================================
# SARIF Output Validation Tests
# ====================================================
option(DTT_TEST_SARIF "Validate SARIF output in tests" ON)

if(DTT_TEST_SARIF AND NOT WIN32)
    find_program(DTT_BASH_EXECUTABLE bash)
    if(NOT DTT_BASH_EXECUTABLE)
        message(FATAL_ERROR "Bash is required for DTT SARIF tests")
    endif()
    set(SARIF_RUNNER ${CMAKE_SOURCE_DIR}/tests/sarif/run_sarif_test.sh)
    set(SARIF_PLUGIN $<TARGET_FILE:DTTPlugin>)

    add_test(NAME sarif_clean_no_results
        COMMAND ${DTT_BASH_EXECUTABLE} ${SARIF_RUNNER} ${CLANGXX}
            ${CMAKE_SOURCE_DIR}/examples/2000_sarif_output_pass.cpp
            ${SARIF_PLUGIN}
            --expect-no-results)

    add_test(NAME sarif_precondition
        COMMAND ${DTT_BASH_EXECUTABLE} ${SARIF_RUNNER} ${CLANGXX}
            ${CMAKE_SOURCE_DIR}/examples/02_basic_fail.cpp
            ${SARIF_PLUGIN}
            --expect-rule DTT-precondition --expect-min-results 2)

    add_test(NAME sarif_null_deref
        COMMAND ${DTT_BASH_EXECUTABLE} ${SARIF_RUNNER} ${CLANGXX}
            ${CMAKE_SOURCE_DIR}/examples/131_nullptr_deref_fail.cpp
            ${SARIF_PLUGIN}
            --expect-rule DTT-null-deref --expect-min-results 1)

    add_test(NAME sarif_resource_leak
        COMMAND ${DTT_BASH_EXECUTABLE} ${SARIF_RUNNER} ${CLANGXX}
            ${CMAKE_SOURCE_DIR}/examples/1000_early_return_leak_fail.cpp
            ${SARIF_PLUGIN}
            --expect-rule DTT-resource-leak --expect-notes)

    set_tests_properties(sarif_clean_no_results sarif_precondition
        sarif_null_deref sarif_resource_leak
        PROPERTIES LABELS "sarif")
endif()

# Isolated review regressions assert each scenario's diagnostic independently.
if(NOT WIN32)
    add_executable(DTTSarifPathTest
        examples/review/sarif_paths.cpp src/DTTSarif.cpp)
    # Exercise SmallVector lifetime checks even in a Release plugin build.
    target_compile_options(DTTSarifPathTest PRIVATE -UNDEBUG)
    target_link_libraries(DTTSarifPathTest PRIVATE clangBasic)
    if(TARGET LLVM)
        target_link_libraries(DTTSarifPathTest PRIVATE LLVM)
    else()
        target_link_libraries(DTTSarifPathTest PRIVATE LLVMSupport)
    endif()
    add_test(NAME review_small_regressions
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/examples/review/small_regressions.py
            --clang ${CLANGXX}
            --plugin $<TARGET_FILE:DTTPlugin>
            --sarif-path-probe $<TARGET_FILE:DTTSarifPathTest>
            --build ${CMAKE_BINARY_DIR} -v)
    set_tests_properties(review_small_regressions PROPERTIES LABELS "review")
    set(DTT_REVIEW_STDLIB_ARGS)
    if(DTT_STDLIB_LIBCXX)
        list(APPEND DTT_REVIEW_STDLIB_ARGS --libcxx)
    endif()
    add_test(NAME review_core_regressions
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/examples/review/core_regressions.py
            --clang ${CLANGXX}
            --plugin $<TARGET_FILE:DTTPlugin> ${DTT_REVIEW_STDLIB_ARGS} -v)
    set_tests_properties(review_core_regressions PROPERTIES LABELS "review")
    add_executable(DTTSymbolicArithmeticTest examples/review/symbolic_arithmetic.cpp)
    if(TARGET LLVM)
        target_link_libraries(DTTSymbolicArithmeticTest PRIVATE LLVM)
    else()
        target_link_libraries(DTTSymbolicArithmeticTest PRIVATE LLVMSupport)
    endif()
    add_test(NAME review_symbolic_arithmetic COMMAND DTTSymbolicArithmeticTest)
    set_tests_properties(review_symbolic_arithmetic PROPERTIES LABELS "review")
endif()

add_test(NAME review_negative_oracle
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/examples/review/negative_oracle_test.py)
set_tests_properties(review_negative_oracle PROPERTIES LABELS "review")

# Exercise the pointer-based Windows va_list ABI on every host without an SDK.
add_test(NAME review_va_list_pointer_pass
    COMMAND ${CLANGXX} ${PLUGIN_ARGS_ENFORCE}
        -target x86_64-pc-windows-msvc -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/review/va_list_pointer_pass.cpp)
add_test(NAME review_va_list_pointer_fail
    COMMAND ${Python3_EXECUTABLE} ${DTT_ERROR_RUNNER}
        ${CMAKE_SOURCE_DIR}/examples/review/va_list_pointer_fail.cpp --
        ${CLANGXX} ${PLUGIN_ARGS_ENFORCE} ${DTT_DIAGNOSTIC_FLAGS}
        -target x86_64-pc-windows-msvc -std=c++20 -fsyntax-only
        ${CMAKE_SOURCE_DIR}/examples/review/va_list_pointer_fail.cpp)
set_tests_properties(review_va_list_pointer_pass review_va_list_pointer_fail
    PROPERTIES LABELS "review")

if(DTT_BUNDLE_Z3)
    add_test(NAME review_runtime_package
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/examples/review/runtime_package.py
            --build ${CMAKE_BINARY_DIR} --clang ${CLANGXX})
    set_tests_properties(review_runtime_package PROPERTIES LABELS "review;packaging")
endif()
