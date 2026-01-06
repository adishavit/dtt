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
    )
    find_library(Z3_LIBRARIES z3
        HINTS
            /usr/lib
            /usr/local/lib
            $ENV{HOMEBREW_PREFIX}/lib
            /opt/homebrew/lib
    )
    if(Z3_INCLUDE_DIRS AND Z3_LIBRARIES)
        message(STATUS "Found Z3: ${Z3_LIBRARIES}")
    else()
        message(FATAL_ERROR "Z3 not found. Install with: apt install z3 (Linux) or brew install z3 (macOS)")
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

# Don't link against LLVM/Clang libs (plugin is loaded into clang)
set_target_properties(DTTPlugin PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"  # macOS
)

# On Linux, use different flags
if(UNIX AND NOT APPLE)
    set_target_properties(DTTPlugin PROPERTIES
        LINK_FLAGS "-Wl,--unresolved-symbols=ignore-all"
    )
endif()

# Disable RTTI to match LLVM/Clang build
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(DTTPlugin PRIVATE -fno-rtti)
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

file(GLOB PASS_TESTS ${CMAKE_SOURCE_DIR}/examples/*_pass.cpp)
file(GLOB FAIL_TESTS ${CMAKE_SOURCE_DIR}/examples/*_fail.cpp)
file(GLOB ALL_TESTS ${CMAKE_SOURCE_DIR}/examples/*.cpp)

foreach(f ${PASS_TESTS})
    get_filename_component(name ${f} NAME_WE)
    add_test(NAME ${name} COMMAND ${CLANGXX} -fplugin=$<TARGET_FILE:DTTPlugin> -std=c++20 -fsyntax-only ${f})
endforeach()

foreach(f ${FAIL_TESTS})
    get_filename_component(name ${f} NAME_WE)
    add_test(NAME ${name} COMMAND ${CLANGXX} -fplugin=$<TARGET_FILE:DTTPlugin> -std=c++20 -fsyntax-only ${f})
    set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
endforeach()

# Warn about non-conforming test files (not *_pass.cpp or *_fail.cpp)
foreach(f ${ALL_TESTS})
    get_filename_component(name ${f} NAME)
    if(NOT name MATCHES "_pass\\.cpp$" AND NOT name MATCHES "_fail\\.cpp$")
        message(WARNING "Test file '${name}' doesn't follow naming convention (*_pass.cpp or *_fail.cpp) - assuming PASS")
        get_filename_component(test_name ${f} NAME_WE)
        add_test(NAME ${test_name} COMMAND ${CLANGXX} -fplugin=$<TARGET_FILE:DTTPlugin> -std=c++20 -fsyntax-only ${f})
    endif()
endforeach()
