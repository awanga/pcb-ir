// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/polygon.hpp"

#include "pcbir/geometry/contour.hpp"

namespace pcbir::geometry {

PolygonValidity validate(const Polygon& polygon) {
  if (validate(polygon.outline) != ContourValidity::Valid) {
    return PolygonValidity::InvalidOutline;
  }
  if (orientation(polygon.outline) != Orientation::CounterClockwise) {
    return PolygonValidity::OutlineWrongOrientation;
  }

  for (const Contour& hole : polygon.holes) {
    if (validate(hole) != ContourValidity::Valid) {
      return PolygonValidity::InvalidHole;
    }
    if (orientation(hole) != Orientation::Clockwise) {
      return PolygonValidity::HoleWrongOrientation;
    }
  }

  return PolygonValidity::Valid;
}

} // namespace pcbir::geometry
