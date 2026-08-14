# SPDX-License-Identifier: Apache-2.0
find_package(Catch2 REQUIRED)
include(Catch)

add_executable(pcbir_tests
  smoke_test.cpp
  core_test.cpp
  geometry/point_test.cpp
  geometry/segment_arc_test.cpp
  geometry/bezier_test.cpp
  geometry/contour_test.cpp
  geometry/polygon_test.cpp
  geometry/path_test.cpp
  geometry/entities_test.cpp
  geometry/serialize_test.cpp
  geometry/boolean_test.cpp
  geometry/spatial_index_test.cpp
  geometry/diagnostics_test.cpp
  connectivity/entities_test.cpp
  connectivity/serialize_test.cpp
  connectivity/diagnostics_test.cpp
  stackup/entities_test.cpp
  stackup/serialize_test.cpp
  stackup/diagnostics_test.cpp
)
target_link_libraries(pcbir_tests PRIVATE pcbir_core Catch2::Catch2WithMain pcbir_warnings)

catch_discover_tests(pcbir_tests)

add_test(
  NAME public_header_hygiene
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_public_header_hygiene.sh
)
