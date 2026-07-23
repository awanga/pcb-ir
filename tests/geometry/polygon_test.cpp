// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::Contour;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::PolygonValidity;
using pcbir::geometry::Segment;
using pcbir::geometry::Span;
using pcbir::geometry::validate;

namespace {

Contour make_triangle(const Point& a, const Point& b, const Point& c) {
  return Contour{.spans = {
                     Span{Segment{.start = a, .end = b}},
                     Span{Segment{.start = b, .end = c}},
                     Span{Segment{.start = c, .end = a}},
                 }};
}

// CounterClockwise by construction (see contour_test.cpp).
Contour ccw_triangle() {
  return make_triangle(Point{.x = 0, .y = 0}, Point{.x = 10, .y = 0}, Point{.x = 0, .y = 10});
}

// Clockwise by construction (see contour_test.cpp).
Contour cw_triangle() {
  return make_triangle(Point{.x = 2, .y = 2}, Point{.x = 2, .y = 5}, Point{.x = 5, .y = 2});
}

} // namespace

TEST_CASE("A CCW outline with no holes is valid", "[geometry][polygon]") {
  const Polygon polygon{.outline = ccw_triangle(), .holes = {}};

  REQUIRE(validate(polygon) == PolygonValidity::Valid);
}

TEST_CASE("A CCW outline with a CW hole is valid", "[geometry][polygon]") {
  const Polygon polygon{.outline = ccw_triangle(), .holes = {cw_triangle()}};

  REQUIRE(validate(polygon) == PolygonValidity::Valid);
}

TEST_CASE("A CW outline is rejected", "[geometry][polygon]") {
  const Polygon polygon{.outline = cw_triangle(), .holes = {}};

  REQUIRE(validate(polygon) == PolygonValidity::OutlineWrongOrientation);
}

TEST_CASE("A CCW hole is rejected", "[geometry][polygon]") {
  const Polygon polygon{.outline = ccw_triangle(), .holes = {ccw_triangle()}};

  REQUIRE(validate(polygon) == PolygonValidity::HoleWrongOrientation);
}

TEST_CASE("A self-intersecting outline is rejected", "[geometry][polygon]") {
  const Contour bowtie{
      .spans = {
          Span{Segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 10}}},
          Span{Segment{.start = Point{.x = 10, .y = 10}, .end = Point{.x = 10, .y = 0}}},
          Span{Segment{.start = Point{.x = 10, .y = 0}, .end = Point{.x = 0, .y = 10}}},
          Span{Segment{.start = Point{.x = 0, .y = 10}, .end = Point{.x = 0, .y = 0}}},
      }};
  const Polygon polygon{.outline = bowtie, .holes = {}};

  REQUIRE(validate(polygon) == PolygonValidity::InvalidOutline);
}
