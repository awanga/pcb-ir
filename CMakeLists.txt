# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)
project(pcbir LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Tiered feature flags. Default OFF: the core library must build with only
# FlatBuffers + Clipper2.
option(PCBIR_BUILD_PYTHON "Build Python bindings (pybind11)" OFF)
option(PCBIR_BUILD_VIZ "Build GPU visualization support" OFF)
option(PCBIR_BUILD_BENCH "Build benchmarks (Google Benchmark)" OFF)
option(PCBIR_BUILD_FUZZ "Build fuzz targets (libFuzzer)" OFF)

include(CTest)

add_library(pcbir_warnings INTERFACE)
target_compile_options(pcbir_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

add_subdirectory(src)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
