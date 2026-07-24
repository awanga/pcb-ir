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

add_library(pcbir_core
  pcbir/version.cpp
  pcbir/geometry/bezier.cpp
  pcbir/geometry/contour.cpp
  pcbir/geometry/polygon.cpp
  pcbir/geometry/path.cpp
  pcbir/geometry/serialize.cpp
  pcbir/geometry/boolean.cpp
)

add_dependencies(pcbir_core pcbir_geometry_schema)

target_include_directories(pcbir_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pcbir_core PRIVATE pcbir_warnings flatbuffers::flatbuffers clipper2::clipper2)
