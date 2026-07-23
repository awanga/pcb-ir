// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::Arc;
using pcbir::geometry::ArcDirection;
using pcbir::geometry::Point;
using pcbir::geometry::Segment;

TEST_CASE("Segment equality is exact endpoint equality", "[geometry][segment]") {
  const Segment a{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 10}};
  const Segment same{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 10}};
  const Segment different{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 11}};

  REQUIRE(a == same);
  REQUIRE_FALSE(a == different);
}

TEST_CASE("Arc::is_valid accepts a center equidistant from both endpoints", "[geometry][arc]") {
  const Arc arc{.start = Point{.x = 10, .y = 0},
                .end = Point{.x = 0, .y = 10},
                .center = Point{.x = 0, .y = 0},
                .direction = ArcDirection::Clockwise};

  REQUIRE(arc.is_valid());
}

TEST_CASE("Arc::is_valid rejects a center that is not equidistant from both endpoints",
          "[geometry][arc]") {
  const Arc arc{.start = Point{.x = 10, .y = 0},
                .end = Point{.x = 0, .y = 5},
                .center = Point{.x = 0, .y = 0},
                .direction = ArcDirection::Clockwise};

  REQUIRE_FALSE(arc.is_valid());
}

TEST_CASE("Arc::is_valid treats start == end as a degenerate full circle", "[geometry][arc]") {
  const Arc arc{.start = Point{.x = 10, .y = 0},
                .end = Point{.x = 10, .y = 0},
                .center = Point{.x = 0, .y = 0},
                .direction = ArcDirection::Clockwise};

  REQUIRE(arc.is_valid());
}

TEST_CASE("Arc::is_valid reports false rather than a wrong answer on overflow", "[geometry][arc]") {
  constexpr int64_t max_int64 = std::numeric_limits<int64_t>::max();
  const Arc arc{.start = Point{.x = max_int64, .y = max_int64},
                .end = Point{.x = 0, .y = 0},
                .center = Point{.x = 0, .y = 0},
                .direction = ArcDirection::Clockwise};

  REQUIRE_FALSE(arc.is_valid());
}

TEST_CASE("Arc equality distinguishes sweep direction", "[geometry][arc]") {
  const Arc clockwise{.start = Point{.x = 10, .y = 0},
                      .end = Point{.x = 0, .y = 10},
                      .center = Point{.x = 0, .y = 0},
                      .direction = ArcDirection::Clockwise};
  const Arc counter_clockwise{.start = Point{.x = 10, .y = 0},
                              .end = Point{.x = 0, .y = 10},
                              .center = Point{.x = 0, .y = 0},
                              .direction = ArcDirection::CounterClockwise};

  REQUIRE_FALSE(clockwise == counter_clockwise);
}
