# SPDX-License-Identifier: Apache-2.0

add_library(pcbir_core
  pcbir/version.cpp
  pcbir/geometry/bezier.cpp
  pcbir/geometry/contour.cpp
  pcbir/geometry/polygon.cpp
  pcbir/geometry/path.cpp
)

target_include_directories(pcbir_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pcbir_core PRIVATE pcbir_warnings)
