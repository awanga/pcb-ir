// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/bbox.hpp"
#include "pcbir/geometry/bezier.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::BBox;
using pcbir::geometry::BEZIER_FLATTEN_DEPTH;
using pcbir::geometry::CubicBezier;
using pcbir::geometry::flatten_cubic_bezier;
using pcbir::geometry::Point;
using pcbir::geometry::Segment;

namespace {
constexpr std::size_t EXPECTED_SEGMENT_COUNT = std::size_t{1} << BEZIER_FLATTEN_DEPTH;
} // namespace

TEST_CASE("flatten_cubic_bezier produces exactly 2^BEZIER_FLATTEN_DEPTH segments",
          "[geometry][bezier]") {
  const CubicBezier curve{.p0 = Point{.x = 0, .y = 0},
                          .p1 = Point{.x = 1000, .y = 5000},
                          .p2 = Point{.x = 4000, .y = 5000},
                          .p3 = Point{.x = 5000, .y = 0}};

  const std::vector<Segment> segments = flatten_cubic_bezier(curve);

  REQUIRE(segments.size() == EXPECTED_SEGMENT_COUNT);
}

TEST_CASE("flatten_cubic_bezier is a connected polyline from p0 to p3", "[geometry][bezier]") {
  const CubicBezier curve{.p0 = Point{.x = 0, .y = 0},
                          .p1 = Point{.x = 1000, .y = 5000},
                          .p2 = Point{.x = 4000, .y = 5000},
                          .p3 = Point{.x = 5000, .y = 0}};

  const std::vector<Segment> segments = flatten_cubic_bezier(curve);

  REQUIRE(segments.front().start == curve.p0);
  REQUIRE(segments.back().end == curve.p3);
  for (std::size_t i = 1; i < segments.size(); ++i) {
    // `i` runs over [1, segments.size()), so both indices are in bounds.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    REQUIRE(segments[i - 1].end == segments[i].start);
  }
}

TEST_CASE("flatten_cubic_bezier of a colinear control polygon is a straight line",
          "[geometry][bezier]") {
  // p0..p3 colinear (evenly spaced along one axis) => the "curve" is
  // actually straight, so every flattened point must fall exactly on it.
  const CubicBezier curve{.p0 = Point{.x = 0, .y = 0},
                          .p1 = Point{.x = 1000, .y = 0},
                          .p2 = Point{.x = 2000, .y = 0},
                          .p3 = Point{.x = 3000, .y = 0}};

  const std::vector<Segment> segments = flatten_cubic_bezier(curve);

  for (const Segment& segment : segments) {
    REQUIRE(segment.start.y == 0);
    REQUIRE(segment.end.y == 0);
  }
}

TEST_CASE("flatten_cubic_bezier stays within the control points' bounding box",
          "[geometry][bezier]") {
  const CubicBezier curve{.p0 = Point{.x = -2000, .y = 0},
                          .p1 = Point{.x = -2000, .y = 3000},
                          .p2 = Point{.x = 2000, .y = 3000},
                          .p3 = Point{.x = 2000, .y = 0}};
  const BBox control_hull = BBox::from_point(curve.p0)
                                .union_with(BBox::from_point(curve.p1))
                                .union_with(BBox::from_point(curve.p2))
                                .union_with(BBox::from_point(curve.p3));

  const std::vector<Segment> segments = flatten_cubic_bezier(curve);

  for (const Segment& segment : segments) {
    REQUIRE(control_hull.contains(segment.start));
    REQUIRE(control_hull.contains(segment.end));
  }
}

TEST_CASE("flatten_cubic_bezier is deterministic across repeated calls", "[geometry][bezier]") {
  const CubicBezier curve{.p0 = Point{.x = 123, .y = -456},
                          .p1 = Point{.x = 789, .y = 1000},
                          .p2 = Point{.x = -321, .y = 654},
                          .p3 = Point{.x = 999, .y = -111}};

  REQUIRE(flatten_cubic_bezier(curve) == flatten_cubic_bezier(curve));
}
