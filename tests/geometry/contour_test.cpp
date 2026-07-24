// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::Arc;
using pcbir::geometry::ArcDirection;
using pcbir::geometry::chords_properly_cross;
using pcbir::geometry::Contour;
using pcbir::geometry::contour_strictly_contains;
using pcbir::geometry::ContourValidity;
using pcbir::geometry::orientation;
using pcbir::geometry::Orientation;
using pcbir::geometry::Point;
using pcbir::geometry::Segment;
using pcbir::geometry::Span;
using pcbir::geometry::span_end;
using pcbir::geometry::span_start;
using pcbir::geometry::validate;

namespace {

Contour make_triangle(const Point& a, const Point& b, const Point& c) {
  return Contour{.spans = {
                     Span{Segment{.start = a, .end = b}},
                     Span{Segment{.start = b, .end = c}},
                     Span{Segment{.start = c, .end = a}},
                 }};
}

} // namespace

TEST_CASE("span_start/span_end read through either alternative", "[geometry][contour]") {
  const Span segment_span = Segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 5, .y = 0}};
  REQUIRE(span_start(segment_span) == Point{.x = 0, .y = 0});
  REQUIRE(span_end(segment_span) == Point{.x = 5, .y = 0});

  const Span arc_span = Arc{.start = Point{.x = 10, .y = 0},
                            .end = Point{.x = 0, .y = 10},
                            .center = Point{.x = 0, .y = 0},
                            .direction = ArcDirection::Clockwise};
  REQUIRE(span_start(arc_span) == Point{.x = 10, .y = 0});
  REQUIRE(span_end(arc_span) == Point{.x = 0, .y = 10});
}

TEST_CASE("A simple triangle contour is valid", "[geometry][contour]") {
  const Contour triangle =
      make_triangle(Point{.x = 0, .y = 0}, Point{.x = 10, .y = 0}, Point{.x = 0, .y = 10});

  REQUIRE(validate(triangle) == ContourValidity::Valid);
  REQUIRE(orientation(triangle) == Orientation::CounterClockwise);
}

TEST_CASE("Reversing a contour's winding flips its orientation", "[geometry][contour]") {
  const Contour reversed =
      make_triangle(Point{.x = 0, .y = 0}, Point{.x = 0, .y = 10}, Point{.x = 10, .y = 0});

  REQUIRE(validate(reversed) == ContourValidity::Valid);
  REQUIRE(orientation(reversed) == Orientation::Clockwise);
}

TEST_CASE("A contour with fewer than 3 spans is invalid", "[geometry][contour]") {
  const Contour two_spans{
      .spans = {
          Span{Segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 0}}},
          Span{Segment{.start = Point{.x = 10, .y = 0}, .end = Point{.x = 0, .y = 0}}},
      }};

  REQUIRE(validate(two_spans) == ContourValidity::TooFewSpans);
}

TEST_CASE("A contour whose spans don't connect is discontinuous", "[geometry][contour]") {
  const Contour broken{
      .spans = {
          Span{Segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 0}}},
          Span{Segment{.start = Point{.x = 99, .y = 99}, .end = Point{.x = 0, .y = 10}}},
          Span{Segment{.start = Point{.x = 0, .y = 10}, .end = Point{.x = 0, .y = 0}}},
      }};

  REQUIRE(validate(broken) == ContourValidity::Discontinuous);
}

TEST_CASE("A contour containing an invalid arc span is rejected", "[geometry][contour]") {
  const Contour with_bad_arc{
      .spans = {
          Span{Arc{.start = Point{.x = 10, .y = 0},
                   .end = Point{.x = 0, .y = 5}, // not equidistant from center
                   .center = Point{.x = 0, .y = 0},
                   .direction = ArcDirection::Clockwise}},
          Span{Segment{.start = Point{.x = 0, .y = 5}, .end = Point{.x = 5, .y = 5}}},
          Span{Segment{.start = Point{.x = 5, .y = 5}, .end = Point{.x = 10, .y = 0}}},
      }};

  REQUIRE(validate(with_bad_arc) == ContourValidity::InvalidArcSpan);
}

TEST_CASE("A self-intersecting (bowtie) contour is rejected", "[geometry][contour]") {
  const Contour bowtie{
      .spans = {
          Span{Segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 10}}},
          Span{Segment{.start = Point{.x = 10, .y = 10}, .end = Point{.x = 10, .y = 0}}},
          Span{Segment{.start = Point{.x = 10, .y = 0}, .end = Point{.x = 0, .y = 10}}},
          Span{Segment{.start = Point{.x = 0, .y = 10}, .end = Point{.x = 0, .y = 0}}},
      }};

  REQUIRE(validate(bowtie) == ContourValidity::SelfIntersecting);
}

TEST_CASE("A contour mixing segments and arcs (a stadium/pill shape) is valid",
          "[geometry][contour]") {
  const Contour pill{
      .spans = {
          Span{Segment{.start = Point{.x = -10, .y = 10}, .end = Point{.x = 10, .y = 10}}},
          Span{Arc{.start = Point{.x = 10, .y = 10},
                   .end = Point{.x = 10, .y = -10},
                   .center = Point{.x = 10, .y = 0},
                   .direction = ArcDirection::Clockwise}},
          Span{Segment{.start = Point{.x = 10, .y = -10}, .end = Point{.x = -10, .y = -10}}},
          Span{Arc{.start = Point{.x = -10, .y = -10},
                   .end = Point{.x = -10, .y = 10},
                   .center = Point{.x = -10, .y = 0},
                   .direction = ArcDirection::Clockwise}},
      }};

  REQUIRE(validate(pill) == ContourValidity::Valid);
}

TEST_CASE("chords_properly_cross detects a transversal crossing but not shared-endpoint touching",
          "[geometry][contour]") {
  REQUIRE(chords_properly_cross(Point{.x = 0, .y = 0},
                                Point{.x = 10, .y = 10},
                                Point{.x = 0, .y = 10},
                                Point{.x = 10, .y = 0}));
  REQUIRE_FALSE(chords_properly_cross(Point{.x = 0, .y = 0},
                                      Point{.x = 10, .y = 0},
                                      Point{.x = 10, .y = 0},
                                      Point{.x = 10, .y = 10}));
  REQUIRE_FALSE(chords_properly_cross(Point{.x = 0, .y = 0},
                                      Point{.x = 10, .y = 0},
                                      Point{.x = 20, .y = 0},
                                      Point{.x = 30, .y = 0}));
}

TEST_CASE("contour_strictly_contains is an exact even-odd point-in-polygon test",
          "[geometry][contour]") {
  const Contour square{
      .spans = {
          Span{Segment{.start = Point{.x = 0, .y = 0}, .end = Point{.x = 10, .y = 0}}},
          Span{Segment{.start = Point{.x = 10, .y = 0}, .end = Point{.x = 10, .y = 10}}},
          Span{Segment{.start = Point{.x = 10, .y = 10}, .end = Point{.x = 0, .y = 10}}},
          Span{Segment{.start = Point{.x = 0, .y = 10}, .end = Point{.x = 0, .y = 0}}},
      }};

  REQUIRE(contour_strictly_contains(square, Point{.x = 5, .y = 5}));
  REQUIRE_FALSE(contour_strictly_contains(square, Point{.x = 50, .y = 50}));
}
