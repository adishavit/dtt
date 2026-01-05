cmake_minimum_required(VERSION 3.16)
project(DTTPlugin)

# Find LLVM and Clang
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

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
