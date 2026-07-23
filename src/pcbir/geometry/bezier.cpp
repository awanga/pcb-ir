// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/bezier.hpp"

#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cstddef>
#include <vector>

namespace pcbir::geometry {

namespace {

// Point::operator+ is already overflow-checked (asserts in debug); this
// only adds one more never-overflowing step (halving never grows a value).
Point midpoint(const Point& a, const Point& b) {
  const Point sum = a + b;
  return Point{.x = sum.x / 2, .y = sum.y / 2};
}

// Bisects `curve` at t=1/2 (standard De Casteljau split) and recurses on
// each half until `depth` reaches 0, appending every leaf endpoint (after
// the very first curve's own p0, pushed once by the caller) to `points`.
// Recursion depth is bounded by BEZIER_FLATTEN_DEPTH (10), so this is safe
// despite misc-no-recursion -- an explicit worklist would only add
// bookkeeping for no benefit at this depth.
// NOLINTNEXTLINE(misc-no-recursion)
void subdivide(const CubicBezier& curve, int depth, std::vector<Point>& points) {
  if (depth == 0) {
    points.push_back(curve.p3);
    return;
  }

  const Point l1 = midpoint(curve.p0, curve.p1);
  const Point mid = midpoint(curve.p1, curve.p2);
  const Point l2 = midpoint(l1, mid);
  const Point r1 = midpoint(curve.p2, curve.p3);
  const Point r2 = midpoint(mid, r1);
  const Point split = midpoint(l2, r2);

  subdivide(CubicBezier{.p0 = curve.p0, .p1 = l1, .p2 = l2, .p3 = split}, depth - 1, points);
  subdivide(CubicBezier{.p0 = split, .p1 = r2, .p2 = r1, .p3 = curve.p3}, depth - 1, points);
}

} // namespace

std::vector<Segment> flatten_cubic_bezier(const CubicBezier& curve) {
  std::vector<Point> points;
  points.reserve((std::size_t{1} << BEZIER_FLATTEN_DEPTH) + 1);
  points.push_back(curve.p0);
  subdivide(curve, BEZIER_FLATTEN_DEPTH, points);

  std::vector<Segment> segments;
  segments.reserve(points.size() - 1);
  for (std::size_t i = 1; i < points.size(); ++i) {
    // `i` runs over [1, points.size()), so both indices are in bounds.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    segments.push_back(Segment{.start = points[i - 1], .end = points[i]});
  }
  return segments;
}

} // namespace pcbir::geometry
