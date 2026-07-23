# SPDX-License-Identifier: Apache-2.0
find_package(Catch2 REQUIRED)
include(Catch)

add_executable(pcbir_tests
  smoke_test.cpp
  core_test.cpp
  geometry/point_test.cpp
  geometry/segment_arc_test.cpp
  geometry/bezier_test.cpp
)
target_link_libraries(pcbir_tests PRIVATE pcbir_core Catch2::Catch2WithMain pcbir_warnings)

catch_discover_tests(pcbir_tests)

add_test(
  NAME public_header_hygiene
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_public_header_hygiene.sh
)
