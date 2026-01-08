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

# Platform-specific plugin loading
# On Windows with clang-cl, use -Xclang -load -Xclang <plugin>
# On Unix, use -fplugin=<plugin>
if(WIN32)
    set(PLUGIN_ARGS -Xclang -load -Xclang $<TARGET_FILE:DTTPlugin> -Xclang -add-plugin -Xclang dtt)
else()
    set(PLUGIN_ARGS -fplugin=$<TARGET_FILE:DTTPlugin>)
endif()

file(GLOB PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/*_pass.cpp)
file(GLOB FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/*_fail.cpp)
file(GLOB XFAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/*_xfail*.cpp)
file(GLOB ALL_TESTS ${CMAKE_SOURCE_DIR}/examples/*.cpp)

foreach(f ${PASS_TESTS})
    get_filename_component(name ${f} NAME_WE)
    add_test(NAME ${name} COMMAND ${CLANGXX} ${PLUGIN_ARGS} -std=c++20 -fsyntax-only ${f})
endforeach()

foreach(f ${FAIL_TESTS})
    get_filename_component(name ${f} NAME_WE)
    add_test(NAME ${name} COMMAND ${CLANGXX} ${PLUGIN_ARGS} -std=c++20 -fsyntax-only ${f})
    set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
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
# To run xfail tests: ctest -L xfail
# To exclude in CI:   ctest -LE xfail
file(GLOB XFAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/*_xfail*.cpp)
foreach(f ${XFAIL_TESTS})
    get_filename_component(name ${f} NAME_WE)
    add_test(NAME ${name} COMMAND ${CLANGXX} ${PLUGIN_ARGS} -std=c++20 -fsyntax-only ${f})
    set_tests_properties(${name} PROPERTIES
        WILL_FAIL TRUE
        LABELS "xfail;future_feature"
    )
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
    add_test(NAME ${name} COMMAND ${CLANGXX} ${PLUGIN_ARGS} -std=c++20 -fsyntax-only ${f})
    set_tests_properties(${name} PROPERTIES
        LABELS "silent;not_tracked"
    )
endforeach()

# Warn about non-conforming test files (not *_pass.cpp, *_fail.cpp, *_xfail.cpp, or *_silent.cpp)
foreach(f ${ALL_TESTS})
    get_filename_component(name ${f} NAME)
    if(NOT name MATCHES "_pass\\.cpp$" AND NOT name MATCHES "_fail\\.cpp$" AND NOT name MATCHES "_xfail" AND NOT name MATCHES "_silent\\.cpp$")
        message(WARNING "Test file '${name}' doesn't follow naming convention (*_pass.cpp, *_fail.cpp, *_xfail.cpp, or *_silent.cpp) - assuming PASS")
        get_filename_component(test_name ${f} NAME_WE)
        add_test(NAME ${test_name} COMMAND ${CLANGXX} ${PLUGIN_ARGS} -std=c++20 -fsyntax-only ${f})
    endif()
endforeach()
