// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/path.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::Arc;
using pcbir::geometry::ArcDirection;
using pcbir::geometry::Path;
using pcbir::geometry::PathValidity;
using pcbir::geometry::Point;
using pcbir::geometry::Segment;
using pcbir::geometry::validate;
using pcbir::geometry::WidthSpan;

TEST_CASE("A single-span path with positive width is valid", "[geometry][path]") {
  const Path path{.spans = {
                      WidthSpan{.geometry = Segment{.start = Point{.x = 0, .y = 0},
                                                    .end = Point{.x = 1000, .y = 0}},
                                .width_nm = 250000},
                  }};

  REQUIRE(validate(path) == PathValidity::Valid);
}

TEST_CASE("An empty path is invalid", "[geometry][path]") {
  const Path path;
  REQUIRE(validate(path) == PathValidity::Empty);
}

TEST_CASE("A path does not need to close, unlike a Contour", "[geometry][path]") {
  const Path path{.spans = {
                      WidthSpan{.geometry = Segment{.start = Point{.x = 0, .y = 0},
                                                    .end = Point{.x = 10, .y = 0}},
                                .width_nm = 1000},
                      WidthSpan{.geometry = Segment{.start = Point{.x = 10, .y = 0},
                                                    .end = Point{.x = 10, .y = 10}},
                                .width_nm = 1000},
                  }};

  // First span's start (0,0) != last span's end (10,10): an open polyline,
  // not a closed loop -- and still Valid.
  REQUIRE(validate(path) == PathValidity::Valid);
}

TEST_CASE("A path with a non-positive width span is rejected", "[geometry][path]") {
  const Path path{.spans = {
                      WidthSpan{.geometry = Segment{.start = Point{.x = 0, .y = 0},
                                                    .end = Point{.x = 10, .y = 0}},
                                .width_nm = 0},
                  }};

  REQUIRE(validate(path) == PathValidity::NonPositiveWidth);
}

TEST_CASE("A path whose spans don't connect is discontinuous", "[geometry][path]") {
  const Path path{.spans = {
                      WidthSpan{.geometry = Segment{.start = Point{.x = 0, .y = 0},
                                                    .end = Point{.x = 10, .y = 0}},
                                .width_nm = 1000},
                      WidthSpan{.geometry = Segment{.start = Point{.x = 99, .y = 99},
                                                    .end = Point{.x = 20, .y = 0}},
                                .width_nm = 1000},
                  }};

  REQUIRE(validate(path) == PathValidity::Discontinuous);
}

TEST_CASE("A path containing an invalid arc span is rejected", "[geometry][path]") {
  const Path path{.spans = {
                      WidthSpan{.geometry = Arc{.start = Point{.x = 10, .y = 0},
                                                .end = Point{.x = 0, .y = 5},
                                                .center = Point{.x = 0, .y = 0},
                                                .direction = ArcDirection::Clockwise},
                                .width_nm = 1000},
                  }};

  REQUIRE(validate(path) == PathValidity::InvalidArcSpan);
}
