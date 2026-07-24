// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/copper_pour.hpp"
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

#include <cstdint>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::core::EntityId;
using pcbir::geometry::Arc;
using pcbir::geometry::ArcDirection;
using pcbir::geometry::Contour;
using pcbir::geometry::CopperPour;
using pcbir::geometry::deserialize_geometry;
using pcbir::geometry::DrillHit;
using pcbir::geometry::GeometrySnapshot;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Keepout;
using pcbir::geometry::LayerRef;
using pcbir::geometry::MaskOpening;
using pcbir::geometry::Pad;
using pcbir::geometry::Path;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::Segment;
using pcbir::geometry::serialize;
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

Path make_path() {
  return Path{.spans = {
                  WidthSpan{.geometry = Segment{.start = Point{.x = 0, .y = 0},
                                                .end = Point{.x = 1000, .y = 0}},
                            .width_nm = 250000},
                  WidthSpan{.geometry = Arc{.start = Point{.x = 1000, .y = 0},
                                            .end = Point{.x = 2000, .y = 1000},
                                            .center = Point{.x = 1000, .y = 1000},
                                            .direction = ArcDirection::CounterClockwise},
                            .width_nm = 250000},
              }};
}

GeometrySnapshot build_sample_snapshot() {
  GeometryWorkspace workspace;

  workspace.insert(Pad{.position = Point{.x = 100, .y = 200},
                       .outline = make_triangle_polygon(),
                       .layer = LayerRef{1}});
  workspace.insert(Via{.position = Point{.x = 0, .y = 0},
                       .drill_diameter_nm = 200000,
                       .finished_hole_diameter_nm = 150000,
                       .pad_diameter_nm = 350000,
                       .start_layer = LayerRef{1},
                       .end_layer = LayerRef{4}});
  workspace.insert(Track{.path = make_path(), .layer = LayerRef{2}});
  workspace.insert(CopperPour{.outline = make_triangle_polygon(), .layer = LayerRef{3}});
  workspace.insert(Keepout{.outline = make_triangle_polygon(), .layer = LayerRef{3}});
  workspace.insert(
      DrillHit{.position = Point{.x = 5, .y = 5}, .diameter_nm = 800000, .plated = true});
  workspace.insert(MaskOpening{.outline = make_triangle_polygon(), .layer = LayerRef{5}});
  workspace.insert(SilkscreenGraphic{.path = make_path(), .layer = LayerRef{6}});

  return workspace.commit();
}

// Serializes and immediately deserializes a fresh sample snapshot, so each
// TEST_CASE below is self-contained (Catch2 discovers/reports on them
// independently, and no single test function accumulates every
// assertion's cognitive complexity).
GeometrySnapshot round_trip(const GeometrySnapshot& original) {
  const std::vector<uint8_t> bytes = serialize(original);
  GeometryWorkspace restored_workspace = deserialize_geometry(bytes);
  return restored_workspace.commit();
}

} // namespace

TEST_CASE("A geometry snapshot round-trip preserves every entity's count",
          "[geometry][serialize]") {
  const GeometrySnapshot original = build_sample_snapshot();
  const GeometrySnapshot restored = round_trip(original);

  REQUIRE(restored.table<Pad>().size() == original.table<Pad>().size());
  REQUIRE(restored.table<Via>().size() == original.table<Via>().size());
  REQUIRE(restored.table<Track>().size() == original.table<Track>().size());
  REQUIRE(restored.table<CopperPour>().size() == original.table<CopperPour>().size());
  REQUIRE(restored.table<Keepout>().size() == original.table<Keepout>().size());
  REQUIRE(restored.table<DrillHit>().size() == original.table<DrillHit>().size());
  REQUIRE(restored.table<MaskOpening>().size() == original.table<MaskOpening>().size());
  REQUIRE(restored.table<SilkscreenGraphic>().size() == original.table<SilkscreenGraphic>().size());
}

TEST_CASE("A Pad round-trips its position, layer, and outline", "[geometry][serialize]") {
  const GeometrySnapshot original = build_sample_snapshot();
  const GeometrySnapshot restored = round_trip(original);

  EntityId original_id;
  original.table<Pad>().for_each([&](EntityId id, const Pad&) { original_id = id; });

  const Pad* restored_pad = restored.table<Pad>().try_get(restored.table<Pad>().find(original_id));
  REQUIRE(restored_pad != nullptr);
  REQUIRE(restored_pad->position == Point{.x = 100, .y = 200});
  REQUIRE(restored_pad->layer == LayerRef{1});
  REQUIRE(restored_pad->outline.outline.spans.size() == 3);
}

TEST_CASE("A Via round-trips every diameter and both layer references", "[geometry][serialize]") {
  const GeometrySnapshot original = build_sample_snapshot();
  const GeometrySnapshot restored = round_trip(original);

  EntityId original_id;
  original.table<Via>().for_each([&](EntityId id, const Via&) { original_id = id; });

  const Via* restored_via = restored.table<Via>().try_get(restored.table<Via>().find(original_id));
  REQUIRE(restored_via != nullptr);
  REQUIRE(restored_via->drill_diameter_nm == 200000);
  REQUIRE(restored_via->finished_hole_diameter_nm == 150000);
  REQUIRE(restored_via->pad_diameter_nm == 350000);
  REQUIRE(restored_via->start_layer == LayerRef{1});
  REQUIRE(restored_via->end_layer == LayerRef{4});
}

TEST_CASE("A Track round-trips a Path mixing segment and arc spans", "[geometry][serialize]") {
  const GeometrySnapshot original = build_sample_snapshot();
  const GeometrySnapshot restored = round_trip(original);

  EntityId original_id;
  original.table<Track>().for_each([&](EntityId id, const Track&) { original_id = id; });

  const Track* restored_track =
      restored.table<Track>().try_get(restored.table<Track>().find(original_id));
  REQUIRE(restored_track != nullptr);
  REQUIRE(restored_track->path.spans.size() == 2);

  const WidthSpan& first = restored_track->path.spans.front();
  const WidthSpan& second = restored_track->path.spans.back();
  REQUIRE(std::holds_alternative<Segment>(first.geometry));
  REQUIRE(std::holds_alternative<Arc>(second.geometry));
  REQUIRE(std::get<Arc>(second.geometry).direction == ArcDirection::CounterClockwise);
}

TEST_CASE("A DrillHit round-trips its diameter and plating flag", "[geometry][serialize]") {
  const GeometrySnapshot original = build_sample_snapshot();
  const GeometrySnapshot restored = round_trip(original);

  EntityId original_id;
  original.table<DrillHit>().for_each([&](EntityId id, const DrillHit&) { original_id = id; });

  const DrillHit* restored_hit =
      restored.table<DrillHit>().try_get(restored.table<DrillHit>().find(original_id));
  REQUIRE(restored_hit != nullptr);
  REQUIRE(restored_hit->plated);
  REQUIRE(restored_hit->diameter_nm == 800000);
}

TEST_CASE("Entity ids are preserved, not reassigned, across the round trip",
          "[geometry][serialize]") {
  const GeometrySnapshot original = build_sample_snapshot();
  const GeometrySnapshot restored = round_trip(original);

  original.table<Pad>().for_each(
      [&](EntityId id, const Pad&) { REQUIRE_FALSE(restored.table<Pad>().find(id).is_null()); });
}
