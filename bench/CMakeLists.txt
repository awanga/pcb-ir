# SPDX-License-Identifier: Apache-2.0
find_package(benchmark REQUIRED)

add_executable(pcbir_board_file_view_bench board_file_view_bench.cpp)
target_link_libraries(pcbir_board_file_view_bench PRIVATE pcbir_core benchmark::benchmark pcbir_warnings)
