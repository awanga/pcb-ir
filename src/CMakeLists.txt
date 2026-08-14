# SPDX-License-Identifier: Apache-2.0

find_package(flatbuffers REQUIRED)
find_package(clipper2 REQUIRED)

# Generates ${CMAKE_SOURCE_DIR}/generated/geometry_generated.h from
# schemas/geometry.fbs (see docs/format-spec.md). build_flatbuffers() also
# adds generated_includes_dir to this directory's include path.
build_flatbuffers(
  "${CMAKE_SOURCE_DIR}/schemas/geometry.fbs"
  ""
  pcbir_geometry_schema
  ""
  "${CMAKE_SOURCE_DIR}/generated"
  ""
  ""
)

# Generates ${CMAKE_SOURCE_DIR}/generated/connectivity_generated.h from
# schemas/connectivity.fbs (TASKS.md Phase 3).
build_flatbuffers(
  "${CMAKE_SOURCE_DIR}/schemas/connectivity.fbs"
  ""
  pcbir_connectivity_schema
  ""
  "${CMAKE_SOURCE_DIR}/generated"
  ""
  ""
)

# Generates ${CMAKE_SOURCE_DIR}/generated/stackup_generated.h from
# schemas/stackup.fbs (TASKS.md Phase 4).
build_flatbuffers(
  "${CMAKE_SOURCE_DIR}/schemas/stackup.fbs"
  ""
  pcbir_stackup_schema
  ""
  "${CMAKE_SOURCE_DIR}/generated"
  ""
  ""
)

add_library(pcbir_core
  pcbir/version.cpp
  pcbir/geometry/bezier.cpp
  pcbir/geometry/contour.cpp
  pcbir/geometry/polygon.cpp
  pcbir/geometry/path.cpp
  pcbir/geometry/serialize.cpp
  pcbir/geometry/boolean.cpp
  pcbir/geometry/spatial_index.cpp
  pcbir/geometry/diagnostics.cpp
  pcbir/connectivity/serialize.cpp
  pcbir/connectivity/diagnostics.cpp
  pcbir/stackup/serialize.cpp
  pcbir/stackup/diagnostics.cpp
)

add_dependencies(pcbir_core pcbir_geometry_schema pcbir_connectivity_schema pcbir_stackup_schema)

target_include_directories(pcbir_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pcbir_core PRIVATE pcbir_warnings flatbuffers::flatbuffers clipper2::clipper2)
