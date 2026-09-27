// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/pad_shape.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <variant>

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::Arc;
using pcbir::geometry::ArcDirection;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::Segment;
using pcbir::kicad::build_pad_outline;
using pcbir::kicad::ImportError;
using pcbir::kicad::KicadPadShape;
using pcbir::kicad::PadShapeParams;

namespace {

// Independently (of pad_shape.cpp) samples the true midpoint of an Arc's
// curve, honoring `direction` -- a stronger check than Arc::is_valid()
// (which only confirms start/end are equidistant from center, not which
// of the two possible arcs `direction` selects). Used to catch a wrong
// ArcDirection that validate()'s chord-based reasoning cannot see.
[[nodiscard]] Point sample_arc_midpoint(const Arc& arc) {
  const auto angle_of = [&arc](const Point& p) {
    return std::atan2(static_cast<double>(p.y - arc.center.y),
                      static_cast<double>(p.x - arc.center.x));
  };
  constexpr double two_pi = 2.0 * std::numbers::pi;
  const double start_angle = angle_of(arc.start);
  double ccw_sweep = angle_of(arc.end) - start_angle;
  while (ccw_sweep < 0.0) {
    ccw_sweep += two_pi;
  }
  while (ccw_sweep >= two_pi) {
    ccw_sweep -= two_pi;
  }
  const double signed_sweep =
      (arc.direction == ArcDirection::CounterClockwise) ? ccw_sweep : ccw_sweep - two_pi;
  const double mid_angle = start_angle + (signed_sweep / 2.0);
  const double radius = std::hypot(static_cast<double>(arc.start.x - arc.center.x),
                                   static_cast<double>(arc.start.y - arc.center.y));
  return Point{.x = arc.center.x + static_cast<int64_t>(std::llround(radius * std::cos(mid_angle))),
               .y =
                   arc.center.y + static_cast<int64_t>(std::llround(radius * std::sin(mid_angle)))};
}

[[nodiscard]] double distance_from_origin(const Point& p) {
  return std::hypot(static_cast<double>(p.x), static_cast<double>(p.y));
}

[[nodiscard]] bool close(int64_t a, int64_t b, int64_t tolerance = 5) {
  return std::abs(a - b) <= tolerance;
}

// Whether one circle-quadrant arc bulges through its own specific expected
// quadrant, identified by its `start` point -- a plain bool (no REQUIRE
// inside) so a caller looping over all 4 arcs can accumulate a single
// pass/fail flag and REQUIRE it once, instead of one REQUIRE per iteration
// (which readability-function-cognitive-complexity penalizes heavily
// inside a loop).
[[nodiscard]] bool circle_arc_bulges_through_expected_quadrant(const Arc& arc) {
  const Point mid = sample_arc_midpoint(arc);
  const auto dist_sq = (mid.x * mid.x) + (mid.y * mid.y);
  if (!close(static_cast<int64_t>(std::llround(std::sqrt(static_cast<double>(dist_sq)))),
             500'000)) {
    return false;
  }

  if (arc.start.x > 0 && arc.start.y == 0) { // east -> north: bulge through Q1.
    return mid.x > 0 && mid.y > 0;
  }
  if (arc.start.y > 0 && arc.start.x == 0) { // north -> west: bulge through Q2.
    return mid.x < 0 && mid.y > 0;
  }
  if (arc.start.x < 0 && arc.start.y == 0) { // west -> south: bulge through Q3.
    return mid.x < 0 && mid.y < 0;
  }
  // south -> east: bulge through Q4.
  return mid.x > 0 && mid.y < 0;
}

// Whether an oval end-cap arc's true midpoint bulges away from the oval's
// own center (the origin, since every pad_shape.cpp output is pad-local),
// not toward it. A plain bool, for the same reason
// circle_arc_bulges_through_expected_quadrant is: it lets a caller looping
// over every arc accumulate one flag and REQUIRE it once.
[[nodiscard]] bool oval_cap_bulges_outward(const Arc& arc) {
  return distance_from_origin(sample_arc_midpoint(arc)) > distance_from_origin(arc.center);
}

} // namespace

TEST_CASE("build_pad_outline builds a centered axis-aligned rectangle for Rect", "[kicad][pads]") {
  const Polygon outline = build_pad_outline(
      PadShapeParams{.shape = KicadPadShape::Rect, .width_nm = 1'600'000, .height_nm = 1'200'000});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(outline.outline.spans.size() == 4);

  bool every_span_is_a_segment = true;
  for (const auto& span : outline.outline.spans) {
    every_span_is_a_segment = every_span_is_a_segment && std::holds_alternative<Segment>(span);
  }
  REQUIRE(every_span_is_a_segment);
}

TEST_CASE("build_pad_outline builds a valid circle for Circle whose 4 arcs sweep outward",
          "[kicad][pads]") {
  const Polygon outline = build_pad_outline(PadShapeParams{
      .shape = KicadPadShape::Circle, .width_nm = 1'000'000, .height_nm = 1'000'000});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(outline.outline.spans.size() == 4);

  // Every point on a circle is equidistant from its center regardless of
  // which way an arc sweeps, so checking the true midpoint's *distance*
  // from the origin can't distinguish a correct 90-degree minor arc from a
  // wrong 270-degree major arc the opposite ArcDirection would produce --
  // both land exactly on the circle. Checking each *specific* arc's
  // midpoint against the exact quadrant its own start/end pair implies
  // does distinguish them (an aggregate "one arc per quadrant" tally does
  // not: flipping every arc's direction merely rotates which arc lands in
  // which quadrant, without changing that each of the 4 quadrants is still
  // hit exactly once). The arc from east=(r,0) to north=(0,r) must bulge
  // through 45 degrees (x>0, y>0); wrong (Clockwise) would bulge through
  // 225 degrees (x<0, y<0) instead -- and likewise, rotated by 90 degrees,
  // for the other 3 arcs.
  bool every_arc_bulges_correctly = true;
  for (const auto& span : outline.outline.spans) {
    every_arc_bulges_correctly = every_arc_bulges_correctly &&
                                 circle_arc_bulges_through_expected_quadrant(std::get<Arc>(span));
  }
  REQUIRE(every_arc_bulges_correctly);
}

TEST_CASE("build_pad_outline rejects a Circle with unequal width and height", "[kicad][pads]") {
  REQUIRE_THROWS_AS(build_pad_outline(PadShapeParams{.shape = KicadPadShape::Circle,
                                                     .width_nm = 1'000'000,
                                                     .height_nm = 500'000}),
                    ImportError);
}

TEST_CASE("build_pad_outline builds a wide stadium for Oval with width > height", "[kicad][pads]") {
  const Polygon outline = build_pad_outline(
      PadShapeParams{.shape = KicadPadShape::Oval, .width_nm = 2'000'000, .height_nm = 1'000'000});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(outline.outline.spans.size() == 4);

  int segment_count = 0;
  int arc_count = 0;
  for (const auto& span : outline.outline.spans) {
    if (std::holds_alternative<Arc>(span)) {
      ++arc_count;
    } else {
      ++segment_count;
    }
  }
  REQUIRE(segment_count == 2);
  REQUIRE(arc_count == 2);
}

TEST_CASE("build_pad_outline's wide Oval caps are centered correctly and bulge outward",
          "[kicad][pads]") {
  // Verified against a real pcbnew-computed pad polygon for size (2, 1)mm
  // (docs/rfcs/0003-kicad-importer-exporter.md): a 500000nm-radius
  // semicircle cap centered at each of (+-500000, 0), joined by straight
  // edges at y = +-500000 spanning x in [-500000, 500000].
  const Polygon outline = build_pad_outline(
      PadShapeParams{.shape = KicadPadShape::Oval, .width_nm = 2'000'000, .height_nm = 1'000'000});

  bool every_cap_centered_correctly = true;
  bool every_cap_bulges_outward = true;
  for (const auto& span : outline.outline.spans) {
    if (const auto* arc = std::get_if<Arc>(&span)) {
      every_cap_centered_correctly = every_cap_centered_correctly &&
                                     close(std::abs(arc->center.x), 500'000) &&
                                     close(arc->center.y, 0);
      every_cap_bulges_outward = every_cap_bulges_outward && oval_cap_bulges_outward(*arc);
    }
  }
  REQUIRE(every_cap_centered_correctly);
  REQUIRE(every_cap_bulges_outward);
}

TEST_CASE("build_pad_outline builds a tall stadium for Oval with height > width", "[kicad][pads]") {
  const Polygon outline = build_pad_outline(
      PadShapeParams{.shape = KicadPadShape::Oval, .width_nm = 1'000'000, .height_nm = 2'000'000});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);

  bool every_cap_centered_correctly = true;
  for (const auto& span : outline.outline.spans) {
    if (const auto* arc = std::get_if<Arc>(&span)) {
      every_cap_centered_correctly = every_cap_centered_correctly && close(arc->center.x, 0) &&
                                     close(std::abs(arc->center.y), 500'000);
    }
  }
  REQUIRE(every_cap_centered_correctly);
}

TEST_CASE("build_pad_outline's Oval degenerates to a circle when width equals height",
          "[kicad][pads]") {
  const Polygon outline = build_pad_outline(
      PadShapeParams{.shape = KicadPadShape::Oval, .width_nm = 1'000'000, .height_nm = 1'000'000});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);

  bool every_span_is_an_arc = true;
  for (const auto& span : outline.outline.spans) {
    every_span_is_an_arc = every_span_is_an_arc && std::holds_alternative<Arc>(span);
  }
  REQUIRE(every_span_is_an_arc);
}

TEST_CASE("build_pad_outline builds an 8-span rounded rectangle for RoundRect", "[kicad][pads]") {
  // Verified against a real pcbnew-computed pad polygon for size (2, 1.5)mm,
  // roundrect_rratio 0.25 (docs/rfcs/0003-kicad-importer-exporter.md):
  // radius = 0.25 * min(2, 1.5) = 0.375mm = 375000nm.
  const Polygon outline = build_pad_outline(PadShapeParams{.shape = KicadPadShape::RoundRect,
                                                           .width_nm = 2'000'000,
                                                           .height_nm = 1'500'000,
                                                           .roundrect_radius_nm = 375'000});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(outline.outline.spans.size() == 8);

  int arc_count = 0;
  bool every_corner_bulges_outward = true;
  for (const auto& span : outline.outline.spans) {
    if (const auto* arc = std::get_if<Arc>(&span)) {
      ++arc_count;
      // A correct, outward-bulging corner's true midpoint must be
      // *further* from the pad's own center (the origin) than its
      // adjacent straight-edge endpoint is -- the defining property of a
      // convex rounded corner. A wrong (inward, 270-degree major-sweep)
      // ArcDirection would instead swing the midpoint back in toward (and
      // typically past) the origin, landing far closer to it than
      // `arc.start` -- this is what actually distinguishes correct from
      // wrong here, since an aggregate check (e.g. "distance > 0") can't:
      // every one of these arcs' true midpoints, right or wrong, is
      // nonzero.
      const Point mid = sample_arc_midpoint(*arc);
      every_corner_bulges_outward = every_corner_bulges_outward &&
                                    distance_from_origin(mid) > distance_from_origin(arc->start);
    }
  }
  REQUIRE(arc_count == 4);
  REQUIRE(every_corner_bulges_outward);
}

TEST_CASE("build_pad_outline's RoundRect degenerates to a rectangle when radius is zero",
          "[kicad][pads]") {
  const Polygon outline = build_pad_outline(PadShapeParams{.shape = KicadPadShape::RoundRect,
                                                           .width_nm = 1'000'000,
                                                           .height_nm = 1'000'000,
                                                           .roundrect_radius_nm = 0});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(outline.outline.spans.size() == 4);
}

TEST_CASE("build_pad_outline builds a sheared quadrilateral for Trapezoid with delta_x",
          "[kicad][pads]") {
  // Verified against a real pcbnew-computed pad polygon for size (2, 1.5)mm,
  // rect_delta (0.5, 0)mm (docs/rfcs/0003-kicad-importer-exporter.md):
  // relative vertices (-1,-1), (1,-0.5), (1,0.5), (-1,1)mm.
  const Polygon outline = build_pad_outline(PadShapeParams{.shape = KicadPadShape::Trapezoid,
                                                           .width_nm = 2'000'000,
                                                           .height_nm = 1'500'000,
                                                           .trapezoid_delta_x_nm = 500'000});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(outline.outline.spans.size() == 4);

  bool every_vertex_on_expected_edge = true;
  for (const auto& span : outline.outline.spans) {
    const auto& segment = std::get<Segment>(span);
    every_vertex_on_expected_edge =
        every_vertex_on_expected_edge && close(std::abs(segment.start.x), 1'000'000);
  }
  REQUIRE(every_vertex_on_expected_edge);
}

TEST_CASE("build_pad_outline's Trapezoid reduces to a rectangle when delta is zero",
          "[kicad][pads]") {
  const Polygon outline = build_pad_outline(PadShapeParams{
      .shape = KicadPadShape::Trapezoid, .width_nm = 1'000'000, .height_nm = 500'000});

  REQUIRE(pcbir::geometry::validate(outline) == pcbir::geometry::PolygonValidity::Valid);

  bool every_vertex_on_expected_edge = true;
  for (const auto& span : outline.outline.spans) {
    const auto& segment = std::get<Segment>(span);
    every_vertex_on_expected_edge =
        every_vertex_on_expected_edge &&
        (close(std::abs(segment.start.x), 500'000) || close(std::abs(segment.start.y), 250'000));
  }
  REQUIRE(every_vertex_on_expected_edge);
}
