// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/rotate.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/export.hpp"
#include "pcbir/kicad/export_footprint.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/import_footprint.hpp"
#include "pcbir/kicad/import_nets.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::ConnectivityWorkspace;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::core::EntityId;
using pcbir::kicad::export_footprints;
using pcbir::kicad::export_layers_section;
using pcbir::kicad::ExportError;
using pcbir::kicad::FootprintImportResult;
using pcbir::kicad::import_footprints;
using pcbir::kicad::import_nets;
using pcbir::kicad::import_stackup;
using pcbir::kicad::parse_sexpr;
using pcbir::kicad::SExpr;
using pcbir::kicad::write_sexpr;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::StackupSnapshot;
using pcbir::stackup::StackupWorkspace;

using pcbir::geometry::Contour;
using pcbir::geometry::Footprint;
using pcbir::geometry::FootprintSide;
using pcbir::geometry::GeometrySnapshot;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Pad;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::Segment;
using pcbir::geometry::Span;
using pcbir::geometry::Via;

namespace {

[[nodiscard]] Layer copper_layer(std::string name) {
  return Layer{.name = std::move(name),
               .kind = LayerKind::Copper,
               .thickness_nm = 0,
               .roughness_nm = 0,
               .material = EntityId{}};
}

[[nodiscard]] StackupSnapshot two_layer_stackup() {
  StackupWorkspace workspace;
  const auto front = workspace.insert(copper_layer("F.Cu"));
  const auto back = workspace.insert(copper_layer("B.Cu"));
  const StackupSnapshot committed = workspace.commit();
  const auto& layers = committed.table<Layer>();
  workspace.insert(LayerStack{.name = "", .layers = {layers.id_of(front), layers.id_of(back)}});
  return workspace.commit();
}

[[nodiscard]] EntityId find_layer_id(const StackupSnapshot& stackup, const std::string& name) {
  EntityId id;
  stackup.table<Layer>().for_each([&](EntityId candidate_id, const Layer& layer) {
    if (layer.name == name) {
      id = candidate_id;
    }
  });
  return id;
}

// An axis-aligned rectangular Polygon (never rotated by the caller-chosen
// footprint rotation -- export_footprints' custom-pad primitives are
// translation-only relative to the pad's own center, independent of
// footprint rotation, so this is a valid "already placed" absolute
// outline for any footprint_rotation_e6 the test picks).
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

[[nodiscard]] SExpr wrap_board(SExpr layers_section, std::vector<SExpr> rest) {
  std::vector<SExpr> children{
      SExpr{.kind = SExpr::Kind::Symbol, .text = "kicad_pcb", .children = {}},
      std::move(layers_section)};
  for (SExpr& node : rest) {
    children.push_back(std::move(node));
  }
  return SExpr{.kind = SExpr::Kind::List, .text = {}, .children = std::move(children)};
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

[[nodiscard]] const Via& only_via(const GeometrySnapshot& geometry) {
  const Via* found = nullptr;
  geometry.table<Via>().for_each([&](EntityId, const Via& candidate) { found = &candidate; });
  REQUIRE(found != nullptr);
  return *found;
}

// Extracted (rather than inlined per-TEST_CASE with a for_each lambda) so
// the lookup's own nesting doesn't count against the calling TEST_CASE's
// readability-function-cognitive-complexity budget -- the same pattern
// import_footprint_test.cpp already uses.
[[nodiscard]] EntityId net_id_by_name(const ConnectivitySnapshot& nets, const std::string& name) {
  EntityId found;
  nets.table<Net>().for_each([&](EntityId id, const Net& net) {
    if (net.name == name) {
      found = id;
    }
  });
  return found;
}

[[nodiscard]] EntityId pin_net_for_pad(const ConnectivitySnapshot& connectivity, EntityId pad_id) {
  EntityId found;
  connectivity.table<Pin>().for_each([&](EntityId, const Pin& pin) {
    if (pin.pad == pad_id) {
      found = pin.net;
    }
  });
  return found;
}

} // namespace

TEST_CASE("export_footprints round-trips a rotated footprint's SMD pad and thru-hole via "
          "through import_footprints",
          "[kicad][export]") {
  const StackupSnapshot stackup = two_layer_stackup();
  const EntityId front_copper = find_layer_id(stackup, "F.Cu");
  const EntityId back_copper = find_layer_id(stackup, "B.Cu");

  ConnectivityWorkspace connectivity_workspace;
  const auto sig1_handle = connectivity_workspace.insert(Net{.name = "SIG1"});
  const auto gnd_handle = connectivity_workspace.insert(Net{.name = "GND"});
  const ConnectivitySnapshot nets_committed = connectivity_workspace.commit();
  const EntityId sig1 = nets_committed.table<Net>().id_of(sig1_handle);
  const EntityId gnd = nets_committed.table<Net>().id_of(gnd_handle);

  // A 30-degree (non-90-multiple) footprint rotation, exercising
  // geometry::rotate's general float path, not just the exact
  // integer-swap 90-degree fast path.
  const Point footprint_position{.x = 10'000'000, .y = 5'000'000};
  constexpr int64_t footprint_rotation_e6 = 30'000'000;

  const Point pad_local_offset{.x = 2'000'000, .y = 1'000'000};
  const Point pad_position =
      footprint_position +
      pcbir::geometry::rotate(pad_local_offset, Point{.x = 0, .y = 0}, footprint_rotation_e6);

  const Point via_local_offset{.x = -3'000'000, .y = 0};
  const Point via_position =
      footprint_position +
      pcbir::geometry::rotate(via_local_offset, Point{.x = 0, .y = 0}, footprint_rotation_e6);

  GeometryWorkspace geometry_workspace;
  const auto pad_handle =
      geometry_workspace.insert(Pad{.position = pad_position,
                                    .outline = rect_outline(pad_position, 800'000, 400'000),
                                    .layer = front_copper,
                                    .pad_number = "1"});
  const EntityId pad_id = geometry_workspace.table<Pad>().id_of(pad_handle);

  const auto via_handle = geometry_workspace.insert(Via{.position = via_position,
                                                        .drill_diameter_nm = 300'000,
                                                        .finished_hole_diameter_nm = 300'000,
                                                        .pad_diameter_nm = 600'000,
                                                        .start_layer = front_copper,
                                                        .end_layer = back_copper,
                                                        .pad_number = "2"});
  const EntityId via_id = geometry_workspace.table<Via>().id_of(via_handle);

  geometry_workspace.insert(Footprint{.reference_designator = "J1",
                                      .value = "Header_2",
                                      .position = footprint_position,
                                      .rotation_e6 = footprint_rotation_e6,
                                      .side = FootprintSide::Top,
                                      .pads = {pad_id, via_id}});
  const GeometrySnapshot original = geometry_workspace.commit();

  ConnectivityWorkspace pin_workspace(nets_committed);
  pin_workspace.insert(Pin{.pad = pad_id, .net = sig1});
  pin_workspace.insert(Pin{.pad = via_id, .net = gnd});
  const ConnectivitySnapshot nets = pin_workspace.commit();

  const std::vector<SExpr> nodes = export_footprints(original, stackup, nets);
  REQUIRE(nodes.size() == 1);

  const SExpr layers_section = export_layers_section(stackup);
  const SExpr root = parse_sexpr(write_sexpr(wrap_board(layers_section, nodes)));
  const StackupSnapshot reimported_stackup = import_stackup(root);
  const ConnectivitySnapshot reimported_nets = import_nets(root);
  const FootprintImportResult result = import_footprints(root, reimported_stackup, reimported_nets);

  REQUIRE(result.geometry.table<Footprint>().size() == 1);
  const Footprint& footprint = only_footprint(result.geometry);
  REQUIRE(footprint.reference_designator == "J1");
  REQUIRE(footprint.value == "Header_2");
  REQUIRE(footprint.side == FootprintSide::Top);
  REQUIRE(footprint.position.x == footprint_position.x);
  REQUIRE(footprint.position.y == footprint_position.y);
  REQUIRE(footprint.rotation_e6 == footprint_rotation_e6);
  REQUIRE(footprint.pads.size() == 2);

  const Pad& reimported_pad = only_pad(result.geometry);
  REQUIRE(std::abs(reimported_pad.position.x - pad_position.x) <= 2);
  REQUIRE(std::abs(reimported_pad.position.y - pad_position.y) <= 2);
  REQUIRE(reimported_pad.pad_number == "1");
  REQUIRE(reimported_pad.outline.outline.spans.size() == 4);

  const Via& reimported_via = only_via(result.geometry);
  REQUIRE(std::abs(reimported_via.position.x - via_position.x) <= 2);
  REQUIRE(std::abs(reimported_via.position.y - via_position.y) <= 2);
  REQUIRE(reimported_via.pad_number == "2");
  REQUIRE(reimported_via.drill_diameter_nm == 300'000);
  REQUIRE(reimported_via.pad_diameter_nm == 600'000);

  const EntityId sig1_reimported = net_id_by_name(reimported_nets, "SIG1");
  const EntityId gnd_reimported = net_id_by_name(reimported_nets, "GND");

  REQUIRE(pin_net_for_pad(result.connectivity, footprint.pads.at(0)) == sig1_reimported);
  REQUIRE(pin_net_for_pad(result.connectivity, footprint.pads.at(1)) == gnd_reimported);
}

TEST_CASE("export_footprints emits Bottom-side layer names for a back-side footprint's pad",
          "[kicad][export]") {
  const StackupSnapshot stackup = two_layer_stackup();
  const EntityId back_copper = find_layer_id(stackup, "B.Cu");

  const Point footprint_position{.x = 0, .y = 0};
  const Point pad_position{.x = 1'000'000, .y = 0};

  GeometryWorkspace geometry_workspace;
  const auto pad_handle =
      geometry_workspace.insert(Pad{.position = pad_position,
                                    .outline = rect_outline(pad_position, 500'000, 500'000),
                                    .layer = back_copper,
                                    .pad_number = "1"});
  const EntityId pad_id = geometry_workspace.table<Pad>().id_of(pad_handle);
  geometry_workspace.insert(Footprint{.reference_designator = "D1",
                                      .value = "",
                                      .position = footprint_position,
                                      .rotation_e6 = 0,
                                      .side = FootprintSide::Bottom,
                                      .pads = {pad_id}});
  const GeometrySnapshot original = geometry_workspace.commit();

  const ConnectivitySnapshot nets = ConnectivityWorkspace{}.commit();
  const std::vector<SExpr> nodes = export_footprints(original, stackup, nets);
  REQUIRE(nodes.size() == 1);

  const SExpr layers_section = export_layers_section(stackup);
  const SExpr root = parse_sexpr(write_sexpr(wrap_board(layers_section, nodes)));
  const StackupSnapshot reimported_stackup = import_stackup(root);
  const ConnectivitySnapshot reimported_nets = import_nets(root);
  const FootprintImportResult result = import_footprints(root, reimported_stackup, reimported_nets);

  const Footprint& footprint = only_footprint(result.geometry);
  REQUIRE(footprint.side == FootprintSide::Bottom);

  const Pad& reimported_pad = only_pad(result.geometry);
  const EntityId reimported_back_copper = find_layer_id(reimported_stackup, "B.Cu");
  REQUIRE(reimported_pad.layer == reimported_back_copper);
}

TEST_CASE("export_footprints throws when a Footprint references a pad id that is "
          "neither a Pad nor a Via",
          "[kicad][export]") {
  const StackupSnapshot stackup = two_layer_stackup();
  const ConnectivitySnapshot nets = ConnectivityWorkspace{}.commit();

  GeometryWorkspace geometry_workspace;
  geometry_workspace.insert(Footprint{.reference_designator = "U1",
                                      .value = "",
                                      .position = {.x = 0, .y = 0},
                                      .rotation_e6 = 0,
                                      .side = FootprintSide::Top,
                                      .pads = {EntityId{999}}});
  const GeometrySnapshot original = geometry_workspace.commit();

  REQUIRE_THROWS_AS(export_footprints(original, stackup, nets), ExportError);
}
