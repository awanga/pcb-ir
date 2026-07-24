// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_POLYGON_HPP
#define PCBIR_GEOMETRY_POLYGON_HPP

#include "pcbir/geometry/contour.hpp"

#include <cstdint>
#include <vector>

namespace pcbir::geometry {

// A polygon over the canonical primitive set: one outer boundary plus zero
// or more holes, each a Contour (docs/architecture.md).
struct Polygon {
  Contour outline;
  std::vector<Contour> holes;
};

enum class PolygonValidity : uint8_t {
  Valid,
  InvalidOutline,
  OutlineWrongOrientation,
  InvalidHole,
  HoleWrongOrientation,
  HoleOutsideOutline,
};

// Validates the outline and every hole (Contour::validate), enforces the
// winding convention (outline CounterClockwise, every hole Clockwise),
// and that each hole is properly contained within the outline: no
// hole-outline chord crossing, and the hole's first vertex strictly
// inside the outline (chords_properly_cross / contour_strictly_contains,
// docs/format-spec.md).
[[nodiscard]] PolygonValidity validate(const Polygon& polygon);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_POLYGON_HPP
