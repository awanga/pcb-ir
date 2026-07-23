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
};

// Validates the outline and every hole (Contour::validate) and enforces
// the winding convention: the outline is CounterClockwise, every hole is
// Clockwise. Does not check that holes actually lie within the outline
// (deferred to the broader geometry validation pass later in this phase).
[[nodiscard]] PolygonValidity validate(const Polygon& polygon);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_POLYGON_HPP
