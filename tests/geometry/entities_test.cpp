// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/arena.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/drill_hit.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/keepout.hpp"
#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/mask_opening.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/path.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/silkscreen_graphic.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"

#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::core::Arena;
using pcbir::core::EntityId;
using pcbir::geometry::BoardOutline;
using pcbir::geometry::Contour;
using pcbir::geometry::CopperPour;
using pcbir::geometry::DrillHit;
using pcbir::geometry::Footprint;
using pcbir::geometry::FootprintSide;
using pcbir::geometry::Keepout;
using pcbir::geometry::LayerRef;
using pcbir::geometry::MaskOpening;
using pcbir::geometry::Pad;
using pcbir::geometry::Path;
using pcbir::geometry::PathValidity;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::PolygonValidity;
using pcbir::geometry::Segment;
using pcbir::geometry::SilkscreenGraphic;
using pcbir::geometry::Span;
using pcbir::geometry::Track;
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

Polygon make_triangle_polygon() {
  return Polygon{.outline = make_triangle(
                     Point{.x = 0, .y = 0}, Point{.x = 10, .y = 0}, Point{.x = 0, .y = 10}),
                 .holes = {}};
}

} // namespace

TEST_CASE("Pad carries a position, outline, layer, and footprint-local pad number",
          "[geometry][entities]") {
  const Pad pad{
      .position = Point{.x = 100, .y = 200},
      .outline = make_triangle_polygon(),
      .layer = LayerRef{1},
      .pad_number = "A1",
  };

  REQUIRE(pad.position == Point{.x = 100, .y = 200});
  REQUIRE(pad.layer == LayerRef{1});
  REQUIRE(pad.pad_number == "A1");
  REQUIRE(validate(pad.outline) == PolygonValidity::Valid);
  REQUIRE(Pad{}.pad_number.empty());
}

TEST_CASE("Via stores drill/pad diameters, derives its annular ring, and carries a pad number",
          "[geometry][entities]") {
  const Via via{
      .position = Point{.x = 0, .y = 0},
      .drill_diameter_nm = 200000,
      .finished_hole_diameter_nm = 150000,
      .pad_diameter_nm = 350000,
      .start_layer = LayerRef{1},
      .end_layer = LayerRef{4},
      .pad_number = "1",
  };

  REQUIRE(via.annular_ring_nm() == (350000 - 150000) / 2);
  REQUIRE(via.start_layer == LayerRef{1});
  REQUIRE(via.end_layer == LayerRef{4});
  REQUIRE(via.pad_number == "1");
}

TEST_CASE("Track wraps a Path with a layer and an optionally-claimed net", "[geometry][entities]") {
  const Track track{
      .path = Path{.spans = {WidthSpan{.geometry = Segment{.start = Point{.x = 0, .y = 0},
                                                           .end = Point{.x = 1000, .y = 0}},
                                       .width_nm = 250000}}},
      .layer = LayerRef{2},
      .net = EntityId{9},
  };

  REQUIRE(validate(track.path) == PathValidity::Valid);
  REQUIRE(track.layer == LayerRef{2});
  REQUIRE(track.net == EntityId{9});
  REQUIRE(Track{}.net.is_null());
}

TEST_CASE("CopperPour wraps a Polygon with a layer and an optionally-claimed net",
          "[geometry][entities]") {
  const CopperPour pour{
      .outline = make_triangle_polygon(), .layer = LayerRef{3}, .net = EntityId{9}};

  REQUIRE(validate(pour.outline) == PolygonValidity::Valid);
  REQUIRE(pour.layer == LayerRef{3});
  REQUIRE(pour.net == EntityId{9});
  REQUIRE(CopperPour{}.net.is_null());
}

TEST_CASE("Keepout wraps a Polygon with a layer", "[geometry][entities]") {
  const Keepout keepout{.outline = make_triangle_polygon(), .layer = LayerRef{3}};

  REQUIRE(validate(keepout.outline) == PolygonValidity::Valid);
  REQUIRE(keepout.layer == LayerRef{3});
}

TEST_CASE("MaskOpening wraps a Polygon with a layer", "[geometry][entities]") {
  const MaskOpening opening{.outline = make_triangle_polygon(), .layer = LayerRef{5}};

  REQUIRE(validate(opening.outline) == PolygonValidity::Valid);
  REQUIRE(opening.layer == LayerRef{5});
}

TEST_CASE("DrillHit stores position, diameter, and plating", "[geometry][entities]") {
  const DrillHit hole{.position = Point{.x = 5, .y = 5}, .diameter_nm = 800000, .plated = true};

  REQUIRE(hole.position == Point{.x = 5, .y = 5});
  REQUIRE(hole.diameter_nm == 800000);
  REQUIRE(hole.plated);
}

TEST_CASE("SilkscreenGraphic wraps a Path with a layer, distinct from Track",
          "[geometry][entities]") {
  const SilkscreenGraphic graphic{
      .path = Path{.spans = {WidthSpan{.geometry = Segment{.start = Point{.x = 0, .y = 0},
                                                           .end = Point{.x = 500, .y = 0}},
                                       .width_nm = 100000}}},
      .layer = LayerRef{6},
  };

  REQUIRE(validate(graphic.path) == PathValidity::Valid);
  static_assert(!std::is_same_v<SilkscreenGraphic, Track>);
}

TEST_CASE("Footprint groups Pad/Via members under a placement and reference designator",
          "[geometry][entities]") {
  const Footprint footprint{
      .reference_designator = "U1",
      .value = "ATMEGA328P",
      .position = Point{.x = 1000000, .y = 2000000},
      .rotation_e6 = 90'000'000,
      .side = FootprintSide::Top,
      .pads = {EntityId{5}, EntityId{6}},
  };

  REQUIRE(footprint.reference_designator == "U1");
  REQUIRE(footprint.value == "ATMEGA328P");
  REQUIRE(footprint.position == Point{.x = 1000000, .y = 2000000});
  REQUIRE(footprint.rotation_e6 == 90'000'000);
  REQUIRE(footprint.side == FootprintSide::Top);
  REQUIRE(footprint.pads == std::vector<EntityId>{EntityId{5}, EntityId{6}});
}

TEST_CASE("A default-constructed Footprint is on the Top side", "[geometry][entities]") {
  REQUIRE(Footprint{}.side == FootprintSide::Top);
}

TEST_CASE("BoardOutline wraps a Polygon with a layer, distinct from CopperPour",
          "[geometry][entities]") {
  const BoardOutline outline{.outline = make_triangle_polygon(), .layer = LayerRef{7}};

  REQUIRE(validate(outline.outline) == PolygonValidity::Valid);
  REQUIRE(outline.layer == LayerRef{7});
  static_assert(!std::is_same_v<BoardOutline, CopperPour>);
}

TEST_CASE("Board entities are distinct component-table types, not interchangeable geometry",
          "[geometry][entities]") {
  Arena<Pad> pads;
  Arena<Keepout> keepouts;

  const auto pad_handle = pads.insert(Pad{.position = Point{.x = 0, .y = 0},
                                          .outline = make_triangle_polygon(),
                                          .layer = LayerRef{1},
                                          .pad_number = "1"},
                                      EntityId{1});
  const auto keepout_handle = keepouts.insert(
      Keepout{.outline = make_triangle_polygon(), .layer = LayerRef{1}}, EntityId{2});

  REQUIRE(pads.get(pad_handle).layer == LayerRef{1});
  REQUIRE(keepouts.get(keepout_handle).layer == LayerRef{1});
  static_assert(!std::is_same_v<decltype(pad_handle), decltype(keepout_handle)>);
}
