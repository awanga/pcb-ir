// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/diagnostics.hpp"
#include "pcbir/geometry/drill_hit.hpp"
#include "pcbir/geometry/keepout.hpp"
#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/mask_opening.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/path.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/silkscreen_graphic.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"

#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::core::EntityId;
using pcbir::geometry::Contour;
using pcbir::geometry::CopperPour;
using pcbir::geometry::Diagnostic;
using pcbir::geometry::DiagnosticCode;
using pcbir::geometry::DrillHit;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Keepout;
using pcbir::geometry::LayerRef;
using pcbir::geometry::MaskOpening;
using pcbir::geometry::Pad;
using pcbir::geometry::Path;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::Segment;
using pcbir::geometry::SilkscreenGraphic;
using pcbir::geometry::Span;
using pcbir::geometry::Track;
using pcbir::geometry::validate;
using pcbir::geometry::Via;
using pcbir::geometry::WidthSpan;

namespace {

Contour make_triangle(const Point& a, const Point& b, const Point& c) {
  return Contour{.spans = {
                     Span{Segment{.start = a, .end = b}},
                     Span{Segment{.start = b, .end = c}},
                     Span{Segment{.start = c, .end = a}},
                 }};
}

// CounterClockwise winding: a Valid outline.
Polygon good_polygon() {
  return Polygon{.outline = make_triangle(
                     Point{.x = 0, .y = 0}, Point{.x = 10, .y = 0}, Point{.x = 0, .y = 10}),
                 .holes = {}};
}

// Clockwise winding: an outline that fails orientation.
Polygon bad_polygon() {
  return Polygon{.outline = make_triangle(
                     Point{.x = 0, .y = 0}, Point{.x = 0, .y = 10}, Point{.x = 10, .y = 0}),
                 .holes = {}};
}

Path good_path() {
  return Path{.spans = {WidthSpan{.geometry = Segment{.start = Point{.x = 0, .y = 0},
                                                      .end = Point{.x = 10, .y = 0}},
                                  .width_nm = 1000}}};
}

} // namespace

TEST_CASE("Pad validation follows its outline's Polygon validity", "[geometry][diagnostics]") {
  REQUIRE(validate(Pad{.position = Point{}, .outline = good_polygon(), .layer = LayerRef{1}}) ==
          DiagnosticCode::Valid);
  REQUIRE(validate(Pad{.position = Point{}, .outline = bad_polygon(), .layer = LayerRef{1}}) ==
          DiagnosticCode::OutlineWrongOrientation);
}

TEST_CASE("CopperPour/Keepout/MaskOpening validation follows their outline's Polygon validity",
          "[geometry][diagnostics]") {
  REQUIRE(validate(CopperPour{.outline = good_polygon(), .layer = LayerRef{1}}) ==
          DiagnosticCode::Valid);
  REQUIRE(validate(CopperPour{.outline = bad_polygon(), .layer = LayerRef{1}}) ==
          DiagnosticCode::OutlineWrongOrientation);
  REQUIRE(validate(Keepout{.outline = good_polygon(), .layer = LayerRef{1}}) ==
          DiagnosticCode::Valid);
  REQUIRE(validate(MaskOpening{.outline = bad_polygon(), .layer = LayerRef{1}}) ==
          DiagnosticCode::OutlineWrongOrientation);
}

TEST_CASE("Track/SilkscreenGraphic validation follows their Path validity",
          "[geometry][diagnostics]") {
  REQUIRE(validate(Track{.path = good_path(), .layer = LayerRef{1}}) == DiagnosticCode::Valid);
  REQUIRE(validate(Track{.path = Path{.spans = {}}, .layer = LayerRef{1}}) ==
          DiagnosticCode::EmptyPath);
  REQUIRE(validate(SilkscreenGraphic{.path = good_path(), .layer = LayerRef{1}}) ==
          DiagnosticCode::Valid);
}

TEST_CASE("DrillHit validation rejects a non-positive diameter", "[geometry][diagnostics]") {
  REQUIRE(validate(DrillHit{.position = Point{}, .diameter_nm = 500000, .plated = true}) ==
          DiagnosticCode::Valid);
  REQUIRE(validate(DrillHit{.position = Point{}, .diameter_nm = 0, .plated = true}) ==
          DiagnosticCode::NonPositiveDrillDiameter);
}

TEST_CASE("Via validation checks every diameter and the derived annular ring",
          "[geometry][diagnostics]") {
  const Via good{.position = Point{},
                 .drill_diameter_nm = 200000,
                 .finished_hole_diameter_nm = 150000,
                 .pad_diameter_nm = 350000,
                 .start_layer = LayerRef{1},
                 .end_layer = LayerRef{4}};
  REQUIRE(validate(good) == DiagnosticCode::Valid);

  Via bad_drill = good;
  bad_drill.drill_diameter_nm = 0;
  REQUIRE(validate(bad_drill) == DiagnosticCode::NonPositiveDrillDiameter);

  Via bad_finished = good;
  bad_finished.finished_hole_diameter_nm = 0;
  REQUIRE(validate(bad_finished) == DiagnosticCode::NonPositiveFinishedHoleDiameter);

  Via bad_pad = good;
  bad_pad.pad_diameter_nm = 0;
  REQUIRE(validate(bad_pad) == DiagnosticCode::NonPositivePadDiameter);

  Via no_annular_ring = good;
  no_annular_ring.pad_diameter_nm = no_annular_ring.finished_hole_diameter_nm;
  REQUIRE(validate(no_annular_ring) == DiagnosticCode::NonPositiveAnnularRing);
}

TEST_CASE("Validating a GeometrySnapshot reports every invalid entity, and only those",
          "[geometry][diagnostics]") {
  GeometryWorkspace workspace;
  workspace.insert(Pad{.position = Point{}, .outline = good_polygon(), .layer = LayerRef{1}});
  workspace.insert(Via{.position = Point{},
                       .drill_diameter_nm = 0,
                       .finished_hole_diameter_nm = 150000,
                       .pad_diameter_nm = 350000,
                       .start_layer = LayerRef{1},
                       .end_layer = LayerRef{4}});
  workspace.insert(DrillHit{.position = Point{}, .diameter_nm = 800000, .plated = true}); // valid

  const auto snapshot = workspace.commit();
  const std::vector<Diagnostic> diagnostics = validate(snapshot);

  REQUIRE(diagnostics.size() == 1);
  REQUIRE(diagnostics.front().code == DiagnosticCode::NonPositiveDrillDiameter);

  EntityId bad_via_id;
  snapshot.table<Via>().for_each([&](EntityId id, const Via&) { bad_via_id = id; });
  REQUIRE(diagnostics.front().id == bad_via_id);
}
