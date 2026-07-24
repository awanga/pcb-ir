// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/polygon.hpp"

#include "pcbir/geometry/contour.hpp"

namespace pcbir::geometry {

namespace {

// Whether every chord of `hole` avoids properly crossing every chord of
// `outline` -- the two contours may still share vertices (adjacency
// within a single contour doesn't apply across two different contours,
// so this checks all pairs).
[[nodiscard]] bool no_chord_crossing(const Contour& outline, const Contour& hole) {
  for (const Span& outer_span : outline.spans) {
    for (const Span& hole_span : hole.spans) {
      if (chords_properly_cross(span_start(outer_span),
                                span_end(outer_span),
                                span_start(hole_span),
                                span_end(hole_span))) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

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
    if (!no_chord_crossing(polygon.outline, hole) ||
        !contour_strictly_contains(polygon.outline, span_start(hole.spans.front()))) {
      return PolygonValidity::HoleOutsideOutline;
    }
  }

  return PolygonValidity::Valid;
}

} // namespace pcbir::geometry
