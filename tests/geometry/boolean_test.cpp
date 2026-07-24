// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/boolean.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::Arc;
using pcbir::geometry::ArcDirection;
using pcbir::geometry::boolean_op;
using pcbir::geometry::BooleanOp;
using pcbir::geometry::Contour;
using pcbir::geometry::Orientation;
using pcbir::geometry::orientation;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::Segment;
using pcbir::geometry::Span;

namespace {

// Two overlapping 10x10 squares, diagonally offset by (5, 5): A = [0,10]^2,
// B = [5,15]^2, overlapping in [5,10]^2. Small, hand-checkable shapes so
// every golden vertex sequence below can be verified independently of
// Clipper2 (docs/format-spec.md documents these as the reference case).

Contour square(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return Contour{
      .spans = {
          Span{Segment{.start = Point{.x = x0, .y = y0}, .end = Point{.x = x1, .y = y0}}},
          Span{Segment{.start = Point{.x = x1, .y = y0}, .end = Point{.x = x1, .y = y1}}},
          Span{Segment{.start = Point{.x = x1, .y = y1}, .end = Point{.x = x0, .y = y1}}},
          Span{Segment{.start = Point{.x = x0, .y = y1}, .end = Point{.x = x0, .y = y0}}},
      }};
}

Contour square_a() {
  return square(0, 0, 10, 10);
}
Contour square_b() {
  return square(5, 5, 15, 15);
}

std::vector<Span> spans_of(std::initializer_list<Point> vertices) {
  std::vector<Span> spans;
  const std::vector<Point> pts(vertices);
  spans.reserve(pts.size());
  for (std::size_t i = 0; i < pts.size(); ++i) {
    // NOLINTNEXTLINE(*-avoid-unchecked-container-access)
    const Point& start = pts[i];
    // NOLINTNEXTLINE(*-avoid-unchecked-container-access)
    const Point& end = pts[(i + 1) % pts.size()];
    spans.emplace_back(Segment{.start = start, .end = end});
  }
  return spans;
}

} // namespace

TEST_CASE("Union of two overlapping squares matches a golden octagon byte-for-byte",
          "[geometry][boolean]") {
  const std::vector<Polygon> result = boolean_op(BooleanOp::Union,
                                                 {Polygon{.outline = square_a(), .holes = {}}},
                                                 {Polygon{.outline = square_b(), .holes = {}}});

  REQUIRE(result.size() == 1);
  REQUIRE(result.front().holes.empty());

  const std::vector<Span> golden = spans_of({
      Point{.x = 10, .y = 5},
      Point{.x = 15, .y = 5},
      Point{.x = 15, .y = 15},
      Point{.x = 5, .y = 15},
      Point{.x = 5, .y = 10},
      Point{.x = 0, .y = 10},
      Point{.x = 0, .y = 0},
      Point{.x = 10, .y = 0},
  });
  REQUIRE(result.front().outline.spans == golden);
  REQUIRE(orientation(result.front().outline) == Orientation::CounterClockwise);
}

TEST_CASE("Intersect of two overlapping squares matches a golden square byte-for-byte",
          "[geometry][boolean]") {
  const std::vector<Polygon> result = boolean_op(BooleanOp::Intersect,
                                                 {Polygon{.outline = square_a(), .holes = {}}},
                                                 {Polygon{.outline = square_b(), .holes = {}}});

  REQUIRE(result.size() == 1);
  REQUIRE(result.front().holes.empty());

  const std::vector<Span> golden = spans_of({
      Point{.x = 10, .y = 10},
      Point{.x = 5, .y = 10},
      Point{.x = 5, .y = 5},
      Point{.x = 10, .y = 5},
  });
  REQUIRE(result.front().outline.spans == golden);
}

TEST_CASE("Difference of two overlapping squares matches a golden hexagon byte-for-byte",
          "[geometry][boolean]") {
  const std::vector<Polygon> result = boolean_op(BooleanOp::Difference,
                                                 {Polygon{.outline = square_a(), .holes = {}}},
                                                 {Polygon{.outline = square_b(), .holes = {}}});

  REQUIRE(result.size() == 1);
  REQUIRE(result.front().holes.empty());

  const std::vector<Span> golden = spans_of({
      Point{.x = 10, .y = 5},
      Point{.x = 5, .y = 5},
      Point{.x = 5, .y = 10},
      Point{.x = 0, .y = 10},
      Point{.x = 0, .y = 0},
      Point{.x = 10, .y = 0},
  });
  REQUIRE(result.front().outline.spans == golden);
}

TEST_CASE("Difference punching a fully-enclosed hole produces one hole, correctly wound",
          "[geometry][boolean]") {
  const Contour outer = square(0, 0, 20, 20);
  const Contour inner = square(5, 5, 15, 15);

  const std::vector<Polygon> result = boolean_op(BooleanOp::Difference,
                                                 {Polygon{.outline = outer, .holes = {}}},
                                                 {Polygon{.outline = inner, .holes = {}}});

  REQUIRE(result.size() == 1);
  REQUIRE(result.front().holes.size() == 1);
  REQUIRE(orientation(result.front().outline) == Orientation::CounterClockwise);
  REQUIRE(orientation(result.front().holes.front()) == Orientation::Clockwise);
}

TEST_CASE("A boolean op is deterministic across repeated runs on the same input",
          "[geometry][boolean]") {
  const std::vector<Polygon> subjects = {Polygon{.outline = square_a(), .holes = {}}};
  const std::vector<Polygon> clips = {Polygon{.outline = square_b(), .holes = {}}};

  const std::vector<Polygon> first = boolean_op(BooleanOp::Union, subjects, clips);
  const std::vector<Polygon> second = boolean_op(BooleanOp::Union, subjects, clips);

  REQUIRE(first.size() == second.size());
  REQUIRE(first.front().outline.spans == second.front().outline.spans);
}

TEST_CASE("A single-subject union with no clips reproduces the same square byte-for-byte",
          "[geometry][boolean]") {
  const std::vector<Polygon> result =
      boolean_op(BooleanOp::Union, {Polygon{.outline = square_a(), .holes = {}}}, {});

  REQUIRE(result.size() == 1);
  REQUIRE(result.front().holes.empty());

  // Same square, same vertex set and winding -- Clipper2 is free to (and
  // does) start the ring at whichever vertex its internal sweep visits
  // first, so the golden sequence is rotated relative to the input's own
  // starting vertex rather than identical to it.
  const std::vector<Span> golden = spans_of({
      Point{.x = 10, .y = 10},
      Point{.x = 0, .y = 10},
      Point{.x = 0, .y = 0},
      Point{.x = 10, .y = 0},
  });
  REQUIRE(result.front().outline.spans == golden);
}

TEST_CASE("An arc-bearing outline flattens to a pure-segment Clipper2 result",
          "[geometry][boolean]") {
  // A full circle of radius 10 centered at (10, 10), expressed as a single
  // degenerate (start == end) Arc span -- Arc's own documented full-circle
  // convention.
  const Contour circle{.spans = {Span{Arc{.start = Point{.x = 20, .y = 10},
                                          .end = Point{.x = 20, .y = 10},
                                          .center = Point{.x = 10, .y = 10},
                                          .direction = ArcDirection::CounterClockwise}}}};

  const std::vector<Polygon> result =
      boolean_op(BooleanOp::Intersect,
                 {Polygon{.outline = circle, .holes = {}}},
                 {Polygon{.outline = square(0, 0, 20, 20), .holes = {}}});

  REQUIRE(result.size() == 1);
  for (const Span& span : result.front().outline.spans) {
    REQUIRE(std::holds_alternative<Segment>(span));
  }
  // The circle already sits inside the square, so intersecting with it
  // should not have clipped anything away: same vertex count as the
  // flattened circle alone would have.
  REQUIRE(result.front().outline.spans.size() >= 32);
}
