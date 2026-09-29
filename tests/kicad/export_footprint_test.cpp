// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/entity_domain.hpp"
#include "pcbir/extension.hpp"
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
#include "pcbir/kicad/pad_shape.hpp"
#include "pcbir/kicad/pad_shape_extension.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::EntityDomain;
using pcbir::Extension;
using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::ConnectivityWorkspace;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::core::EntityId;
using pcbir::kicad::decode_pad_shape_extension;
using pcbir::kicad::DecodedPadShape;
using pcbir::kicad::export_footprints;
using pcbir::kicad::export_layers_section;
using pcbir::kicad::ExportError;
using pcbir::kicad::FootprintImportResult;
using pcbir::kicad::import_footprints;
using pcbir::kicad::import_nets;
using pcbir::kicad::import_stackup;
using pcbir::kicad::KicadPadShape;
using pcbir::kicad::PAD_SHAPE_EXTENSION_NAME;
using pcbir::kicad::PAD_SHAPE_EXTENSION_NAMESPACE;
using pcbir::kicad::PAD_SHAPE_EXTENSION_VERSION;
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

// REQUIREs `opt` is present and returns its value -- lets every call site
// below dereference the result of decode_pad_shape_extension without
// tripping bugprone-unchecked-optional-access (clang-tidy doesn't treat
// Catch2's REQUIRE(opt.has_value()) macro as narrowing `opt`'s type, so
// each dereference downstream still looks unchecked to it; centralizing
// the one real check plus its NOLINT here keeps every other line clean).
[[nodiscard]] DecodedPadShape require_decoded(const std::optional<DecodedPadShape>& opt) {
  REQUIRE(opt.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  return *opt;
}

// A roundrect SMD pad, verifying the PCBIR_KICAD/pad_shape extension's
// ratio-to-radius conversion (the trickiest of the 5 shape kinds).
constexpr const char* ROUNDRECT_BOARD = R"(
  (kicad_pcb
    (layers (0 "F.Cu" signal) (2 "B.Cu" signal))
    (footprint "" (layer "F.Cu") (at 10 10)
      (pad "1" smd roundrect (at 0 0) (size 2 1) (roundrect_rratio 0.25) (layers "F.Cu")))
  )
)";

// A custom-shape SMD pad -- must produce no PCBIR_KICAD/pad_shape
// extension at all (Approximated, not Preserved, fallback path).
constexpr const char* CUSTOM_PAD_BOARD = R"(
  (kicad_pcb (layers (0 "F.Cu" signal)) (footprint "" (layer "F.Cu") (at 10 5)
    (pad "1" smd custom (at 2 1) (size 0.5 0.5) (layers "F.Cu")
      (primitives
        (gr_poly (pts (xy -0.8 -0.6) (xy 0.8 -0.6) (xy 0.8 0.6) (xy -0.8 0.6)) (width 0))))))
)";

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

TEST_CASE("import_footprints/export_footprints round-trip a roundrect pad's shape as Preserved "
          "via the PCBIR_KICAD/pad_shape extension",
          "[kicad][export]") {
  const SExpr root = parse_sexpr(ROUNDRECT_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const FootprintImportResult imported = import_footprints(root, stackup, nets);

  REQUIRE(imported.extensions.size() == 1);
  const Extension& extension = imported.extensions.at(0);
  REQUIRE(extension.domain == EntityDomain::Geometry);
  REQUIRE(extension.ext_namespace == PAD_SHAPE_EXTENSION_NAMESPACE);
  REQUIRE(extension.name == PAD_SHAPE_EXTENSION_NAME);
  REQUIRE(extension.version == PAD_SHAPE_EXTENSION_VERSION);

  const DecodedPadShape decoded = require_decoded(decode_pad_shape_extension(extension.payload));
  REQUIRE(decoded.params.shape == KicadPadShape::RoundRect);
  REQUIRE(decoded.params.width_nm == 2'000'000);
  REQUIRE(decoded.params.height_nm == 1'000'000);
  REQUIRE(decoded.params.roundrect_radius_nm == 250'000); // 0.25 * min(2mm, 1mm)

  const std::vector<SExpr> nodes =
      export_footprints(imported.geometry, stackup, imported.connectivity, imported.extensions);
  REQUIRE(nodes.size() == 1);

  const std::string exported_text = write_sexpr(nodes.at(0));
  REQUIRE(exported_text.find("roundrect") != std::string::npos);
  REQUIRE(exported_text.find("custom") == std::string::npos);

  const SExpr layers_section = export_layers_section(stackup);
  const SExpr reexported_root = parse_sexpr(write_sexpr(wrap_board(layers_section, nodes)));
  const StackupSnapshot reimported_stackup = import_stackup(reexported_root);
  const ConnectivitySnapshot reimported_nets = import_nets(reexported_root);
  const FootprintImportResult reimported =
      import_footprints(reexported_root, reimported_stackup, reimported_nets);

  REQUIRE(reimported.extensions.size() == 1); // still Preserved on a second round-trip
  const DecodedPadShape redecoded =
      require_decoded(decode_pad_shape_extension(reimported.extensions.at(0).payload));
  REQUIRE(redecoded.params.shape == decoded.params.shape);
  REQUIRE(redecoded.params.width_nm == decoded.params.width_nm);
  REQUIRE(redecoded.params.height_nm == decoded.params.height_nm);
  REQUIRE(redecoded.params.roundrect_radius_nm == decoded.params.roundrect_radius_nm);

  const Pad& reimported_pad = only_pad(reimported.geometry);
  REQUIRE(reimported_pad.outline.outline.spans.size() == 8); // 4 edges + 4 rounded corners
}

TEST_CASE("import_footprints produces no pad_shape extension for a custom-shape pad, and "
          "export_footprints still falls back to a custom shape",
          "[kicad][export]") {
  const SExpr root = parse_sexpr(CUSTOM_PAD_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const FootprintImportResult imported = import_footprints(root, stackup, nets);

  REQUIRE(imported.extensions.empty());

  const std::vector<SExpr> nodes =
      export_footprints(imported.geometry, stackup, imported.connectivity, imported.extensions);
  REQUIRE(nodes.size() == 1);

  const std::string exported_text = write_sexpr(nodes.at(0));
  REQUIRE(exported_text.find("custom") != std::string::npos);
}
