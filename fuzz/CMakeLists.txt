# SPDX-License-Identifier: Apache-2.0

# libFuzzer (-fsanitize=fuzzer) is a Clang-only feature -- PCBIR_BUILD_FUZZ
# is only buildable with Clang (see CONTRIBUTING.md).
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR
    "PCBIR_BUILD_FUZZ requires Clang (libFuzzer via -fsanitize=fuzzer); "
    "current compiler is ${CMAKE_CXX_COMPILER_ID}. See CONTRIBUTING.md.")
endif()

add_executable(pcbir_geometry_fuzz geometry_fuzz.cpp)
target_link_libraries(pcbir_geometry_fuzz PRIVATE pcbir_core)
target_compile_options(pcbir_geometry_fuzz PRIVATE -fsanitize=fuzzer,address)
target_link_options(pcbir_geometry_fuzz PRIVATE -fsanitize=fuzzer,address)
