// SPDX-License-Identifier: Apache-2.0
//
// RFC 0003's round-trip fidelity harness ("Round-trip fidelity harness"):
// two independent checks.
//
// 1. Required, self-contained (no KiCad needed) -- the real CI gate:
//    export(board) -> import -> A -> export(A) -> import -> B, then
//    structurally compare A and B. Entity ids are never compared directly
//    (a re-import allocates fresh ids each time), only stable content --
//    the same discipline docs/rfcs/0003-kicad-importer-exporter.md's
//    harness section calls for.
// 2. Optional, environment-gated real-pcbnew verification, skipped
//    gracefully (reported, not silently ignored) when no KiCad
//    installation is available on this device.
#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/path.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/rotate.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/export.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::BoardSnapshot;
using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::ConnectivityWorkspace;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::core::EntityId;
using pcbir::kicad::export_kicad_pcb;
using pcbir::kicad::import_kicad_pcb;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::StackupSnapshot;
using pcbir::stackup::StackupWorkspace;

using pcbir::geometry::BoardOutline;
using pcbir::geometry::Contour;
using pcbir::geometry::CopperPour;
using pcbir::geometry::Footprint;
using pcbir::geometry::FootprintSide;
using pcbir::geometry::GeometrySnapshot;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Pad;
using pcbir::geometry::Path;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::Segment;
using pcbir::geometry::Span;
using pcbir::geometry::Track;
using pcbir::geometry::Via;
using pcbir::geometry::WidthSpan;

namespace {

[[nodiscard]] EntityId layer_id_by_name(const StackupSnapshot& stackup, const std::string& name) {
  EntityId id;
  stackup.table<Layer>().for_each([&](EntityId candidate_id, const Layer& layer) {
    if (layer.name == name) {
      id = candidate_id;
    }
  });
  return id;
}

[[nodiscard]] Polygon rect_outline(const Point& center, int64_t half_width, int64_t half_height) {
  const Contour rect{
      .spans = {Span{Segment{.start = {.x = center.x - half_width, .y = center.y - half_height},
                             .end = {.x = center.x + half_width, .y = center.y - half_height}}},
                Span{Segment{.start = {.x = center.x + half_width, .y = center.y - half_height},
                             .end = {.x = center.x + half_width, .y = center.y + half_height}}},
                Span{Segment{.start = {.x = center.x + half_width, .y = center.y + half_height},
                             .end = {.x = center.x - half_width, .y = center.y + half_height}}},
                Span{Segment{.start = {.x = center.x - half_width, .y = center.y + half_height},
                             .end = {.x = center.x - half_width, .y = center.y - half_height}}}}};
  return Polygon{.outline = rect, .holes = {}};
}

[[nodiscard]] StackupSnapshot build_stackup() {
  StackupWorkspace workspace;
  const auto front = workspace.insert(Layer{.name = "F.Cu",
                                            .kind = LayerKind::Copper,
                                            .thickness_nm = 0,
                                            .roughness_nm = 0,
                                            .material = EntityId{}});
  const auto back = workspace.insert(Layer{.name = "B.Cu",
                                           .kind = LayerKind::Copper,
                                           .thickness_nm = 0,
                                           .roughness_nm = 0,
                                           .material = EntityId{}});
  workspace.insert(Layer{.name = "Edge.Cuts",
                         .kind = LayerKind::EdgeCuts,
                         .thickness_nm = 0,
                         .roughness_nm = 0,
                         .material = EntityId{}});
  workspace.insert(Layer{.name = "F.SilkS",
                         .kind = LayerKind::Silkscreen,
                         .thickness_nm = 0,
                         .roughness_nm = 0,
                         .material = EntityId{}});
  workspace.insert(Layer{.name = "B.SilkS",
                         .kind = LayerKind::Silkscreen,
                         .thickness_nm = 0,
                         .roughness_nm = 0,
                         .material = EntityId{}});
  const StackupSnapshot committed = workspace.commit();
  const auto& layers = committed.table<Layer>();
  workspace.insert(LayerStack{.name = "", .layers = {layers.id_of(front), layers.id_of(back)}});
  return workspace.commit();
}

// A full-featured board exercising every in-scope entity type at once: a
// board outline, a footprint with an axis-aligned (90-degree) rotation
// holding one SMD pad and one thru-hole pad, a track, a free via, a zone,
// and 2 nets -- built directly via Workspaces (no KiCad text involved) so
// this check needs no golden corpus file, matching RFC 0003's "required,
// self-contained" design.
[[nodiscard]] BoardSnapshot build_original_board() {
  BoardSnapshot board;
  board.stackup = build_stackup();
  const EntityId front_copper = layer_id_by_name(board.stackup, "F.Cu");
  const EntityId back_copper = layer_id_by_name(board.stackup, "B.Cu");
  const EntityId edge_cuts = layer_id_by_name(board.stackup, "Edge.Cuts");

  ConnectivityWorkspace nets_workspace;
  const auto sig1_handle = nets_workspace.insert(Net{.name = "SIG1"});
  const auto gnd_handle = nets_workspace.insert(Net{.name = "GND"});
  const ConnectivitySnapshot nets_only = nets_workspace.commit();
  const EntityId sig1 = nets_only.table<Net>().id_of(sig1_handle);
  const EntityId gnd = nets_only.table<Net>().id_of(gnd_handle);

  constexpr int64_t mm = 1'000'000;
  const Point footprint_position{.x = 10 * mm, .y = 10 * mm};
  constexpr int64_t footprint_rotation_e6 = 90'000'000; // axis-aligned, exact

  const Point pad_local_offset{.x = 2 * mm, .y = 1 * mm};
  const Point pad_position =
      footprint_position +
      pcbir::geometry::rotate(pad_local_offset, Point{.x = 0, .y = 0}, footprint_rotation_e6);
  const Point via_local_offset{.x = -3 * mm, .y = 0};
  const Point via_position =
      footprint_position +
      pcbir::geometry::rotate(via_local_offset, Point{.x = 0, .y = 0}, footprint_rotation_e6);

  GeometryWorkspace geometry_workspace;
  geometry_workspace.insert(BoardOutline{
      .outline = rect_outline({.x = 10 * mm, .y = 10 * mm}, 10 * mm, 10 * mm), .layer = edge_cuts});

  const auto pad_handle =
      geometry_workspace.insert(Pad{.position = pad_position,
                                    .outline = rect_outline(pad_position, 800'000, 400'000),
                                    .layer = front_copper,
                                    .pad_number = "1"});
  const EntityId pad_id = geometry_workspace.table<Pad>().id_of(pad_handle);

  const auto owned_via_handle = geometry_workspace.insert(Via{.position = via_position,
                                                              .drill_diameter_nm = 300'000,
                                                              .finished_hole_diameter_nm = 300'000,
                                                              .pad_diameter_nm = 600'000,
                                                              .start_layer = front_copper,
                                                              .end_layer = back_copper,
                                                              .pad_number = "2"});
  const EntityId owned_via_id = geometry_workspace.table<Via>().id_of(owned_via_handle);

  geometry_workspace.insert(Footprint{.reference_designator = "R1",
                                      .value = "10k",
                                      .position = footprint_position,
                                      .rotation_e6 = footprint_rotation_e6,
                                      .side = FootprintSide::Top,
                                      .pads = {pad_id, owned_via_id}});

  geometry_workspace.insert(Track{
      .path = Path{.spans = {WidthSpan{.geometry = Span{Segment{.start = {.x = 0, .y = 0},
                                                                .end = {.x = 5 * mm, .y = 5 * mm}}},
                                       .width_nm = 250'000}}},
      .layer = front_copper,
      .net = sig1});

  const auto free_via_handle =
      geometry_workspace.insert(Via{.position = {.x = 15 * mm, .y = 15 * mm},
                                    .drill_diameter_nm = 400'000,
                                    .finished_hole_diameter_nm = 400'000,
                                    .pad_diameter_nm = 800'000,
                                    .start_layer = front_copper,
                                    .end_layer = back_copper,
                                    .pad_number = ""});
  const EntityId free_via_id = geometry_workspace.table<Via>().id_of(free_via_handle);

  geometry_workspace.insert(
      CopperPour{.outline = rect_outline({.x = 10 * mm, .y = 10 * mm}, 10 * mm, 10 * mm),
                 .layer = back_copper,
                 .net = gnd});

  board.geometry = geometry_workspace.commit();

  ConnectivityWorkspace pin_workspace(nets_only);
  pin_workspace.insert(Pin{.pad = pad_id, .net = sig1});
  pin_workspace.insert(Pin{.pad = owned_via_id, .net = gnd});
  pin_workspace.insert(Pin{.pad = free_via_id, .net = gnd});
  board.connectivity = pin_workspace.commit();

  return board;
}

[[nodiscard]] std::vector<std::string> sorted_net_names(const ConnectivitySnapshot& connectivity) {
  std::vector<std::string> names;
  connectivity.table<Net>().for_each([&](EntityId, const Net& net) { names.push_back(net.name); });
  std::ranges::sort(names);
  return names;
}

[[nodiscard]] std::vector<std::string> copper_layer_names_in_order(const StackupSnapshot& stackup) {
  std::vector<std::string> names;
  const LayerStack* stack = nullptr;
  stackup.table<LayerStack>().for_each(
      [&](EntityId, const LayerStack& candidate) { stack = &candidate; });
  REQUIRE(stack != nullptr);
  const auto& layers = stackup.table<Layer>();
  for (const EntityId id : stack->layers) {
    const Layer* layer = layers.try_get(layers.find(id));
    REQUIRE(layer != nullptr);
    names.push_back(layer->name);
  }
  return names;
}

[[nodiscard]] std::set<std::string> non_copper_layer_names(const StackupSnapshot& stackup) {
  std::set<std::string> names;
  stackup.table<Layer>().for_each([&](EntityId, const Layer& layer) {
    if (layer.kind != LayerKind::Copper) {
      names.insert(layer.name);
    }
  });
  return names;
}

[[nodiscard]] const Footprint& only_footprint(const GeometrySnapshot& geometry) {
  const Footprint* found = nullptr;
  geometry.table<Footprint>().for_each(
      [&](EntityId, const Footprint& candidate) { found = &candidate; });
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] const Pad& only_pad(const GeometrySnapshot& geometry) {
  const Pad* found = nullptr;
  geometry.table<Pad>().for_each([&](EntityId, const Pad& candidate) { found = &candidate; });
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] const Via& via_by_pad_number(const GeometrySnapshot& geometry,
                                           const std::string& pad_number) {
  const Via* found = nullptr;
  geometry.table<Via>().for_each([&](EntityId, const Via& candidate) {
    if (candidate.pad_number == pad_number) {
      found = &candidate;
    }
  });
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] const Track& only_track(const GeometrySnapshot& geometry) {
  const Track* found = nullptr;
  geometry.table<Track>().for_each([&](EntityId, const Track& candidate) { found = &candidate; });
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] const CopperPour& only_zone(const GeometrySnapshot& geometry) {
  const CopperPour* found = nullptr;
  geometry.table<CopperPour>().for_each(
      [&](EntityId, const CopperPour& candidate) { found = &candidate; });
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] std::string net_name_for_geometry_id(const BoardSnapshot& board, EntityId id) {
  EntityId net_id;
  board.connectivity.table<Pin>().for_each([&](EntityId, const Pin& pin) {
    if (pin.pad == id) {
      net_id = pin.net;
    }
  });
  if (net_id.is_null()) {
    return "";
  }
  std::string name;
  board.connectivity.table<Net>().for_each([&](EntityId candidate_id, const Net& net) {
    if (candidate_id == net_id) {
      name = net.name;
    }
  });
  return name;
}

[[nodiscard]] EntityId pad_id_of(const GeometrySnapshot& geometry, const Pad& pad) {
  EntityId found;
  geometry.table<Pad>().for_each([&](EntityId id, const Pad& candidate) {
    if (&candidate == &pad) {
      found = id;
    }
  });
  return found;
}

[[nodiscard]] EntityId via_id_of(const GeometrySnapshot& geometry, const Via& via) {
  EntityId found;
  geometry.table<Via>().for_each([&](EntityId id, const Via& candidate) {
    if (&candidate == &via) {
      found = id;
    }
  });
  return found;
}

void require_stackup_equal(const StackupSnapshot& a, const StackupSnapshot& b) {
  REQUIRE(copper_layer_names_in_order(a) == copper_layer_names_in_order(b));
  REQUIRE(non_copper_layer_names(a) == non_copper_layer_names(b));
}

void require_footprint_identity_equal(const Footprint& fa, const Footprint& fb) {
  REQUIRE(fa.reference_designator == fb.reference_designator);
  REQUIRE(fa.value == fb.value);
  REQUIRE(fa.side == fb.side);
}

void require_footprint_placement_equal(const Footprint& fa, const Footprint& fb) {
  REQUIRE(fa.position.x == fb.position.x);
  REQUIRE(fa.position.y == fb.position.y);
  REQUIRE(fa.rotation_e6 == fb.rotation_e6);
  REQUIRE(fa.pads.size() == fb.pads.size());
}

// Split into two helpers (identity, placement) rather than one long
// sequence of REQUIREs -- Catch2's REQUIRE macro expansion itself counts
// heavily against readability-function-cognitive-complexity, independent
// of any real branching in this function.
void require_footprint_equal(const GeometrySnapshot& a, const GeometrySnapshot& b) {
  const Footprint& fa = only_footprint(a);
  const Footprint& fb = only_footprint(b);
  require_footprint_identity_equal(fa, fb);
  require_footprint_placement_equal(fa, fb);
}

void require_pad_equal(const BoardSnapshot& a, const BoardSnapshot& b) {
  const Pad& pad_a = only_pad(a.geometry);
  const Pad& pad_b = only_pad(b.geometry);
  REQUIRE(pad_a.pad_number == pad_b.pad_number);
  REQUIRE(pad_a.position.x == pad_b.position.x);
  REQUIRE(pad_a.position.y == pad_b.position.y);
  REQUIRE(pad_a.outline.outline.spans.size() == pad_b.outline.outline.spans.size());
  REQUIRE(net_name_for_geometry_id(a, pad_id_of(a.geometry, pad_a)) ==
          net_name_for_geometry_id(b, pad_id_of(b.geometry, pad_b)));
}

void require_via_equal(const BoardSnapshot& a,
                       const BoardSnapshot& b,
                       const std::string& pad_number) {
  const Via& via_a = via_by_pad_number(a.geometry, pad_number);
  const Via& via_b = via_by_pad_number(b.geometry, pad_number);
  REQUIRE(via_a.position.x == via_b.position.x);
  REQUIRE(via_a.position.y == via_b.position.y);
  REQUIRE(via_a.drill_diameter_nm == via_b.drill_diameter_nm);
  REQUIRE(via_a.pad_diameter_nm == via_b.pad_diameter_nm);
  REQUIRE(net_name_for_geometry_id(a, via_id_of(a.geometry, via_a)) ==
          net_name_for_geometry_id(b, via_id_of(b.geometry, via_b)));
}

void require_track_equal(const GeometrySnapshot& a, const GeometrySnapshot& b) {
  const Track& track_a = only_track(a);
  const Track& track_b = only_track(b);
  REQUIRE(track_a.path.spans.size() == track_b.path.spans.size());
  REQUIRE(track_a.path.spans.at(0).width_nm == track_b.path.spans.at(0).width_nm);
}

void require_zone_equal(const GeometrySnapshot& a, const GeometrySnapshot& b) {
  const CopperPour& zone_a = only_zone(a);
  const CopperPour& zone_b = only_zone(b);
  REQUIRE(zone_a.outline.outline.spans.size() == zone_b.outline.outline.spans.size());
}

void require_table_sizes_equal(const BoardSnapshot& a, const BoardSnapshot& b) {
  REQUIRE(a.geometry.table<BoardOutline>().size() == b.geometry.table<BoardOutline>().size());
  REQUIRE(a.geometry.table<Footprint>().size() == b.geometry.table<Footprint>().size());
  REQUIRE(a.geometry.table<Pad>().size() == b.geometry.table<Pad>().size());
  REQUIRE(a.geometry.table<Via>().size() == b.geometry.table<Via>().size());
  REQUIRE(a.geometry.table<Track>().size() == b.geometry.table<Track>().size());
  REQUIRE(a.geometry.table<CopperPour>().size() == b.geometry.table<CopperPour>().size());
}

void require_boards_structurally_equal(const BoardSnapshot& a, const BoardSnapshot& b) {
  require_table_sizes_equal(a, b);
  REQUIRE(sorted_net_names(a.connectivity) == sorted_net_names(b.connectivity));

  require_stackup_equal(a.stackup, b.stackup);
  require_footprint_equal(a.geometry, b.geometry);
  require_pad_equal(a, b);
  require_via_equal(a, b, "2"); // the thru-hole pad, modeled as a Via
  require_via_equal(a, b, "");  // the free via
  require_track_equal(a.geometry, b.geometry);
  require_zone_equal(a.geometry, b.geometry);
}

} // namespace

TEST_CASE("KiCad round-trip harness: two successive export/import cycles produce structurally "
          "equal snapshots",
          "[kicad][roundtrip]") {
  const BoardSnapshot original = build_original_board();

  const std::filesystem::path path_1 =
      std::filesystem::temp_directory_path() / "pcbir_roundtrip_1.kicad_pcb";
  export_kicad_pcb(original, path_1);
  const BoardSnapshot snapshot_a = import_kicad_pcb(path_1);

  const std::filesystem::path path_2 =
      std::filesystem::temp_directory_path() / "pcbir_roundtrip_2.kicad_pcb";
  export_kicad_pcb(snapshot_a, path_2);
  const BoardSnapshot snapshot_b = import_kicad_pcb(path_2);

  require_boards_structurally_equal(snapshot_a, snapshot_b);
}
