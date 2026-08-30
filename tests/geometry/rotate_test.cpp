// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/rotate.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::Arc;
using pcbir::geometry::ArcDirection;
using pcbir::geometry::Contour;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::rotate;
using pcbir::geometry::Segment;
using pcbir::geometry::Span;

TEST_CASE("Rotating by 0 or 360 degrees is the identity", "[geometry][rotate]") {
  const Point point{.x = 1000, .y = 2000};
  const Point origin{.x = 0, .y = 0};

  REQUIRE(rotate(point, origin, 0) == point);
  REQUIRE(rotate(point, origin, 360'000'000) == point);
  REQUIRE(rotate(point, origin, -360'000'000) == point);
}

TEST_CASE("Rotating by exact 90-degree multiples about the origin is exact integer swap/negate",
          "[geometry][rotate]") {
  const Point point{.x = 1000, .y = 2000};
  const Point origin{.x = 0, .y = 0};

  REQUIRE(rotate(point, origin, 90'000'000) == Point{.x = -2000, .y = 1000});
  REQUIRE(rotate(point, origin, 180'000'000) == Point{.x = -1000, .y = -2000});
  REQUIRE(rotate(point, origin, 270'000'000) == Point{.x = 2000, .y = -1000});
}

TEST_CASE("A negative or more-than-one-turn angle normalizes to the same exact result",
          "[geometry][rotate]") {
  const Point point{.x = 1000, .y = 2000};
  const Point origin{.x = 0, .y = 0};

  REQUIRE(rotate(point, origin, -90'000'000) == rotate(point, origin, 270'000'000));
  REQUIRE(rotate(point, origin, 450'000'000) == rotate(point, origin, 90'000'000));
}

TEST_CASE(
    "Rotation about a non-origin point rotates the relative offset, not the absolute position",
    "[geometry][rotate]") {
  const Point point{.x = 1100, .y = 2000};
  const Point origin{.x = 100, .y = 0};

  // Relative to origin: (1000, 2000) -> 90 degrees CCW -> (-2000, 1000).
  REQUIRE(rotate(point, origin, 90'000'000) == Point{.x = 100 - 2000, .y = 0 + 1000});
}

TEST_CASE("Rotating a point about itself is the identity for any angle", "[geometry][rotate]") {
  const Point point{.x = 12345, .y = -6789};

  REQUIRE(rotate(point, point, 90'000'000) == point);
  REQUIRE(rotate(point, point, 37'500'000) == point);
}

TEST_CASE("A non-90-degree rotation matches the trigonometric result within one nanometer",
          "[geometry][rotate]") {
  const Point point{.x = 1000, .y = 0};
  const Point origin{.x = 0, .y = 0};

  const Point rotated = rotate(point, origin, 45'000'000);

  const double radians = 45.0 * (std::numbers::pi / 180.0);
  const auto expected_x = static_cast<int64_t>(std::llround(1000.0 * std::cos(radians)));
  const auto expected_y = static_cast<int64_t>(std::llround(1000.0 * std::sin(radians)));
  REQUIRE(rotated.x == expected_x);
  REQUIRE(rotated.y == expected_y);
}

TEST_CASE("Rotating a Segment rotates both endpoints", "[geometry][rotate]") {
  const Segment segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 1000, .y = 0}};
  const Point origin{.x = 0, .y = 0};

  const Segment rotated = rotate(segment, origin, 90'000'000);

  REQUIRE(rotated.start == Point{.x = 0, .y = 0});
  REQUIRE(rotated.end == Point{.x = 0, .y = 1000});
}

TEST_CASE("Rotating an Arc rotates its start/end/center and preserves direction",
          "[geometry][rotate]") {
  const Arc arc{.start = Point{.x = 1000, .y = 0},
                .end = Point{.x = 0, .y = 1000},
                .center = Point{.x = 0, .y = 0},
                .direction = ArcDirection::CounterClockwise};
  const Point origin{.x = 0, .y = 0};

  const Arc rotated = rotate(arc, origin, 90'000'000);

  REQUIRE(rotated.start == Point{.x = 0, .y = 1000});
  REQUIRE(rotated.end == Point{.x = -1000, .y = 0});
  REQUIRE(rotated.center == Point{.x = 0, .y = 0});
  REQUIRE(rotated.direction == ArcDirection::CounterClockwise);
}

TEST_CASE("Rotating a Contour preserves span count and connectivity", "[geometry][rotate]") {
  const Contour contour{
      .spans = {
          Span{Segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 0}}},
          Span{Segment{.start = Point{.x = 10, .y = 0}, .end = Point{.x = 0, .y = 10}}},
          Span{Segment{.start = Point{.x = 0, .y = 10}, .end = Point{.x = 0, .y = 0}}},
      }};
  const Point origin{.x = 0, .y = 0};

  const Contour rotated = rotate(contour, origin, 90'000'000);

  REQUIRE(rotated.spans.size() == contour.spans.size());
  REQUIRE(pcbir::geometry::validate(rotated) == pcbir::geometry::validate(contour));
}

TEST_CASE("Rotating a Polygon rotates its outline and every hole", "[geometry][rotate]") {
  const Contour outline{
      .spans = {
          Span{Segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 0}}},
          Span{Segment{.start = Point{.x = 10, .y = 0}, .end = Point{.x = 0, .y = 10}}},
          Span{Segment{.start = Point{.x = 0, .y = 10}, .end = Point{.x = 0, .y = 0}}},
      }};
  const Polygon polygon{.outline = outline, .holes = {outline}};
  const Point origin{.x = 0, .y = 0};

  const Polygon rotated = rotate(polygon, origin, 90'000'000);

  REQUIRE(rotated.outline.spans.size() == polygon.outline.spans.size());
  REQUIRE(rotated.holes.size() == polygon.holes.size());
  REQUIRE(rotated.holes.front().spans.size() == polygon.holes.front().spans.size());
}
