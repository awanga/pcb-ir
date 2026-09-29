// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/path.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/import_nets.hpp"
#include "pcbir/kicad/import_track.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::Net;
using pcbir::core::EntityId;
using pcbir::kicad::import_nets;
using pcbir::kicad::import_stackup;
using pcbir::kicad::import_tracks;
using pcbir::kicad::ImportError;
using pcbir::kicad::parse_sexpr;
using pcbir::kicad::SExpr;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::StackupSnapshot;

using pcbir::geometry::Arc;
using pcbir::geometry::GeometrySnapshot;
using pcbir::geometry::Segment;
using pcbir::geometry::Track;

namespace {

// One straight segment and one arc, both on F.Cu and referencing SIG1 --
// verified against real pcbnew output (docs/rfcs/0003-kicad-importer-
// exporter.md). A rounded-corner probe already confirmed the same
// three-point arc conversion this reuses from import_geometry.cpp.
constexpr const char* TRACK_BOARD = R"(
  (kicad_pcb
    (layers (0 "F.Cu" signal) (2 "B.Cu" signal))
    (segment (start 0 0) (end 5 5) (width 0.25) (layer "F.Cu") (net "SIG1"))
    (arc (start 0 5) (mid 1.464466 1.464466) (end 5 0) (width 0.3) (layer "F.Cu") (net "SIG1"))
    (segment (start 1 1) (end 2 2) (width 0.2) (layer "B.Cu"))
  )
)";

[[nodiscard]] EntityId net_id_by_name(const ConnectivitySnapshot& nets, const std::string& name) {
  EntityId found;
  nets.table<Net>().for_each([&](EntityId id, const Net& net) {
    if (net.name == name) {
      found = id;
    }
  });
  return found;
}

[[nodiscard]] EntityId front_copper_layer_id(const StackupSnapshot& stackup) {
  EntityId found;
  stackup.table<Layer>().for_each([&](EntityId id, const Layer& layer) {
    if (layer.kind == LayerKind::Copper && layer.name == "F.Cu") {
      found = id;
    }
  });
  return found;
}

// Extracted (rather than inlined per-TEST_CASE with a for_each lambda) so
// the lookup's own nesting doesn't count against the calling TEST_CASE's
// readability-function-cognitive-complexity budget.
[[nodiscard]] const Track& segment_track_with_width(const GeometrySnapshot& geometry,
                                                    int64_t width_nm) {
  const Track* found = nullptr;
  geometry.table<Track>().for_each([&](EntityId, const Track& track) {
    if (std::holds_alternative<Segment>(track.path.spans.front().geometry) &&
        track.path.spans.front().width_nm == width_nm) {
      found = &track;
    }
  });
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] const Track& arc_track(const GeometrySnapshot& geometry) {
  const Track* found = nullptr;
  geometry.table<Track>().for_each([&](EntityId, const Track& track) {
    if (std::holds_alternative<Arc>(track.path.spans.front().geometry)) {
      found = &track;
    }
  });
  REQUIRE(found != nullptr);
  return *found;
}

} // namespace

TEST_CASE("import_tracks imports one Track per segment/arc entry, never merged",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(TRACK_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_tracks(root, stackup, nets);

  REQUIRE(geometry.table<Track>().size() == 3);

  int single_span_tracks = 0;
  geometry.table<Track>().for_each([&](EntityId, const Track& track) {
    if (track.path.spans.size() == 1) {
      ++single_span_tracks;
    }
  });
  REQUIRE(single_span_tracks == 3);
}

TEST_CASE("import_tracks resolves a segment's exact endpoints, width, layer, and net",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(TRACK_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_tracks(root, stackup, nets);

  const Track& segment_track = segment_track_with_width(geometry, 250'000);
  const auto& segment = std::get<Segment>(segment_track.path.spans.front().geometry);
  REQUIRE(segment.start.x == 0);
  REQUIRE(segment.start.y == 0);
  REQUIRE(segment.end.x == 5'000'000);
  REQUIRE(segment.end.y == 5'000'000);
  REQUIRE(segment_track.layer == front_copper_layer_id(stackup));
  REQUIRE(segment_track.net == net_id_by_name(nets, "SIG1"));
}

TEST_CASE("import_tracks converts a track arc via the same three-point circumcenter formula",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(TRACK_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_tracks(root, stackup, nets);

  const Track& track = arc_track(geometry);
  const auto& arc = std::get<Arc>(track.path.spans.front().geometry);
  REQUIRE(arc.is_valid());
  REQUIRE(std::abs(arc.center.x - 5'000'000) <= 2);
  REQUIRE(std::abs(arc.center.y - 5'000'000) <= 2);
}

TEST_CASE("import_tracks leaves an unconnected segment's net null", "[kicad][import]") {
  const SExpr root = parse_sexpr(TRACK_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_tracks(root, stackup, nets);

  const Track* back_track = nullptr;
  EntityId back_copper;
  stackup.table<Layer>().for_each([&](EntityId id, const Layer& layer) {
    if (layer.kind == LayerKind::Copper && layer.name == "B.Cu") {
      back_copper = id;
    }
  });
  geometry.table<Track>().for_each([&](EntityId, const Track& track) {
    if (track.layer == back_copper) {
      back_track = &track;
    }
  });
  REQUIRE(back_track != nullptr);
  REQUIRE(back_track->net.is_null());
}

TEST_CASE("import_tracks throws on a segment missing (width ...)", "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal)) (segment (start 0 0) (end 1 1) (layer "F.Cu"))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  REQUIRE_THROWS_AS(import_tracks(root, stackup, nets), ImportError);
}

TEST_CASE("import_tracks throws when a segment's layer isn't recognized", "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal))
           (segment (start 0 0) (end 1 1) (width 0.2) (layer "Dwgs.User"))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  REQUIRE_THROWS_AS(import_tracks(root, stackup, nets), ImportError);
}

TEST_CASE("import_tracks given a base snapshot continues its id space instead of restarting "
          "at 1",
          "[kicad][import]") {
  // Mirrors how import_kicad_pcb chains composable passes together
  // (pcbir/kicad/import.hpp).
  const SExpr root = parse_sexpr(TRACK_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot first = import_tracks(root, stackup, nets);
  const GeometrySnapshot second = import_tracks(root, stackup, nets, first);

  REQUIRE(first.table<Track>().size() == 3);
  REQUIRE(second.table<Track>().size() == 6);

  bool no_collision = true;
  int seen_from_first = 0;
  first.table<Track>().for_each([&](EntityId first_id, const Track&) {
    int matches = 0;
    second.table<Track>().for_each(
        [&](EntityId second_id, const Track&) { matches += (second_id == first_id) ? 1 : 0; });
    seen_from_first += matches;
    no_collision = no_collision && matches <= 1;
  });
  REQUIRE(seen_from_first == 3); // every id from `first` carried forward unchanged
  REQUIRE(no_collision);         // and none of the 3 new Tracks reused one of those ids
}
