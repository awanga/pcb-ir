// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/export.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/import_geometry.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::core::EntityId;
using pcbir::kicad::export_board_outline;
using pcbir::kicad::export_layers_section;
using pcbir::kicad::ExportError;
using pcbir::kicad::import_board_outline;
using pcbir::kicad::import_stackup;
using pcbir::kicad::parse_sexpr;
using pcbir::kicad::SExpr;
using pcbir::kicad::write_sexpr;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::StackupSnapshot;
using pcbir::stackup::StackupWorkspace;

using pcbir::geometry::Arc;
using pcbir::geometry::ArcDirection;
using pcbir::geometry::BoardOutline;
using pcbir::geometry::Contour;
using pcbir::geometry::GeometrySnapshot;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Point;
using pcbir::geometry::Polygon;
using pcbir::geometry::Segment;
using pcbir::geometry::Span;

namespace {

// Layer's non-name/kind fields are irrelevant to export_layers_section
// (thickness/roughness/material only matter for stackup validation, not
// tested here), but are still spelled out on every construction to match
// this codebase's designated-initializer convention (import.cpp always
// spells every field; a partial designated-init list here would otherwise
// trip -Wmissing-designated-field-initializers under clang-tidy).
constexpr int64_t NO_THICKNESS_NM = 0;
constexpr int64_t NO_ROUGHNESS_NM = 0;

[[nodiscard]] StackupSnapshot two_layer_stackup() {
  StackupWorkspace workspace;
  const auto front = workspace.insert(Layer{.name = "F.Cu",
                                            .kind = LayerKind::Copper,
                                            .thickness_nm = NO_THICKNESS_NM,
                                            .roughness_nm = NO_ROUGHNESS_NM,
                                            .material = EntityId{}});
  const auto back = workspace.insert(Layer{.name = "B.Cu",
                                           .kind = LayerKind::Copper,
                                           .thickness_nm = NO_THICKNESS_NM,
                                           .roughness_nm = NO_ROUGHNESS_NM,
                                           .material = EntityId{}});
  workspace.insert(Layer{.name = "Edge.Cuts",
                         .kind = LayerKind::EdgeCuts,
                         .thickness_nm = NO_THICKNESS_NM,
                         .roughness_nm = NO_ROUGHNESS_NM,
                         .material = EntityId{}});
  workspace.insert(Layer{.name = "F.SilkS",
                         .kind = LayerKind::Silkscreen,
                         .thickness_nm = NO_THICKNESS_NM,
                         .roughness_nm = NO_ROUGHNESS_NM,
                         .material = EntityId{}});
  workspace.insert(Layer{.name = "B.SilkS",
                         .kind = LayerKind::Silkscreen,
                         .thickness_nm = NO_THICKNESS_NM,
                         .roughness_nm = NO_ROUGHNESS_NM,
                         .material = EntityId{}});
  const StackupSnapshot committed = workspace.commit();
  const auto& layers = committed.table<Layer>();
  workspace.insert(LayerStack{.name = "", .layers = {layers.id_of(front), layers.id_of(back)}});
  return workspace.commit();
}

[[nodiscard]] Layer copper_layer(std::string name) {
  return Layer{.name = std::move(name),
               .kind = LayerKind::Copper,
               .thickness_nm = NO_THICKNESS_NM,
               .roughness_nm = NO_ROUGHNESS_NM,
               .material = EntityId{}};
}

[[nodiscard]] StackupSnapshot four_layer_stackup() {
  StackupWorkspace workspace;
  const auto front = workspace.insert(copper_layer("F.Cu"));
  const auto in1 = workspace.insert(copper_layer("In1.Cu"));
  const auto in2 = workspace.insert(copper_layer("In2.Cu"));
  const auto back = workspace.insert(copper_layer("B.Cu"));
  const StackupSnapshot committed = workspace.commit();
  const auto& layers = committed.table<Layer>();
  workspace.insert(LayerStack{
      .name = "",
      .layers = {layers.id_of(front), layers.id_of(in1), layers.id_of(in2), layers.id_of(back)}});
  return workspace.commit();
}

// Finds the single Arc span among `spans`, or nullptr if there is none --
// extracted from its one caller (the arc round-trip test, below) to keep
// that TEST_CASE's cognitive complexity under the project's threshold
// (a for/if/REQUIRE nest directly inside a TEST_CASE compounds heavily
// via Catch2's own REQUIRE macro expansion).
[[nodiscard]] const Arc* find_arc_span(const std::vector<Span>& spans) {
  for (const Span& span : spans) {
    if (const auto* arc = std::get_if<Arc>(&span)) {
      return arc;
    }
  }
  return nullptr;
}

// A `(kicad_pcb (layers ...) ...rest)` root wrapping `rest` (already-built
// nodes, e.g. export_board_outline's output) alongside `layers_section` --
// enough structure for import_stackup/import_board_outline to consume,
// used to check the exporter's output through the real importer as the
// oracle rather than re-implementing the parsing logic in the test itself.
[[nodiscard]] SExpr wrap_board(SExpr layers_section, std::vector<SExpr> rest) {
  std::vector<SExpr> children{
      SExpr{.kind = SExpr::Kind::Symbol, .text = "kicad_pcb", .children = {}},
      std::move(layers_section)};
  for (SExpr& node : rest) {
    children.push_back(std::move(node));
  }
  return SExpr{.kind = SExpr::Kind::List, .text = {}, .children = std::move(children)};
}

} // namespace

TEST_CASE("export_layers_section round-trips a 2-layer stackup through import_stackup",
          "[kicad][export]") {
  const StackupSnapshot original = two_layer_stackup();
  const SExpr layers_section = export_layers_section(original);

  const SExpr root = parse_sexpr(write_sexpr(wrap_board(layers_section, {})));
  const StackupSnapshot reimported = import_stackup(root);

  REQUIRE(reimported.table<Layer>().size() == original.table<Layer>().size());
  REQUIRE(reimported.table<LayerStack>().size() == 1);

  const LayerStack* stack = nullptr;
  reimported.table<LayerStack>().for_each(
      [&](EntityId, const LayerStack& candidate) { stack = &candidate; });
  REQUIRE(stack != nullptr);
  REQUIRE(stack->layers.size() == 2);

  const auto& layers = reimported.table<Layer>();
  REQUIRE(layers.try_get(layers.find(stack->layers.at(0)))->name == "F.Cu");
  REQUIRE(layers.try_get(layers.find(stack->layers.at(1)))->name == "B.Cu");
}

TEST_CASE("export_layers_section keeps inner Copper layers in physical order for a 4-layer "
          "stackup, round-tripped through import_stackup",
          "[kicad][export]") {
  const StackupSnapshot original = four_layer_stackup();
  const SExpr layers_section = export_layers_section(original);

  const SExpr root = parse_sexpr(write_sexpr(wrap_board(layers_section, {})));
  const StackupSnapshot reimported = import_stackup(root);

  const LayerStack* stack = nullptr;
  reimported.table<LayerStack>().for_each(
      [&](EntityId, const LayerStack& candidate) { stack = &candidate; });
  REQUIRE(stack != nullptr);
  REQUIRE(stack->layers.size() == 4);

  const auto& layers = reimported.table<Layer>();
  std::vector<std::string> names;
  names.reserve(stack->layers.size());
  for (const EntityId id : stack->layers) {
    names.push_back(layers.try_get(layers.find(id))->name);
  }
  REQUIRE(names == std::vector<std::string>{"F.Cu", "In1.Cu", "In2.Cu", "B.Cu"});
}

TEST_CASE("export_layers_section assigns KiCad's real fixed numeric layer ids", "[kicad][export]") {
  const SExpr section = export_layers_section(four_layer_stackup());
  // (layers (0 "F.Cu" signal) (4 "In1.Cu" signal) (6 "In2.Cu" signal)
  //         (2 "B.Cu" signal))
  REQUIRE(section.children.at(0).text == "layers");
  REQUIRE(section.children.size() == 5);
  REQUIRE(section.children.at(1).children.at(0).text == "0");
  REQUIRE(section.children.at(2).children.at(0).text == "4");
  REQUIRE(section.children.at(3).children.at(0).text == "6");
  REQUIRE(section.children.at(4).children.at(0).text == "2");
}

TEST_CASE("export_layers_section throws when the LayerStack has more copper layers than "
          "KiCad's id scheme supports",
          "[kicad][export]") {
  StackupWorkspace workspace;
  std::vector<EntityId> ids;
  ids.reserve(33);
  for (int i = 0; i < 33; ++i) {
    const auto handle = workspace.insert(copper_layer("Cu" + std::to_string(i)));
    const StackupSnapshot snap = workspace.commit();
    ids.push_back(snap.table<Layer>().id_of(handle));
  }
  workspace.insert(LayerStack{.name = "", .layers = ids});
  REQUIRE_THROWS_AS(export_layers_section(workspace.commit()), ExportError);
}

TEST_CASE("export_board_outline round-trips a straight-edged rectangle through "
          "import_board_outline",
          "[kicad][export]") {
  const StackupSnapshot stackup = two_layer_stackup();
  EntityId edge_cuts_id;
  stackup.table<Layer>().for_each([&](EntityId id, const Layer& layer) {
    if (layer.kind == LayerKind::EdgeCuts) {
      edge_cuts_id = id;
    }
  });

  GeometryWorkspace geometry_workspace;
  const Contour rect{
      .spans = {Span{Segment{.start = {.x = 0, .y = 0}, .end = {.x = 50'000'000, .y = 0}}},
                Span{Segment{.start = {.x = 50'000'000, .y = 0},
                             .end = {.x = 50'000'000, .y = 30'000'000}}},
                Span{Segment{.start = {.x = 50'000'000, .y = 30'000'000},
                             .end = {.x = 0, .y = 30'000'000}}},
                Span{Segment{.start = {.x = 0, .y = 30'000'000}, .end = {.x = 0, .y = 0}}}}};
  geometry_workspace.insert(
      BoardOutline{.outline = Polygon{.outline = rect, .holes = {}}, .layer = edge_cuts_id});
  const GeometrySnapshot original = geometry_workspace.commit();

  const std::vector<SExpr> nodes = export_board_outline(original, stackup);
  REQUIRE(nodes.size() == 4);

  const SExpr layers_section = export_layers_section(stackup);
  const SExpr root = parse_sexpr(write_sexpr(wrap_board(layers_section, nodes)));
  const StackupSnapshot reimported_stackup = import_stackup(root);
  const GeometrySnapshot reimported = import_board_outline(root, reimported_stackup);

  REQUIRE(reimported.table<BoardOutline>().size() == 1);
  const BoardOutline* outline = nullptr;
  reimported.table<BoardOutline>().for_each(
      [&](EntityId, const BoardOutline& candidate) { outline = &candidate; });
  REQUIRE(outline != nullptr);
  REQUIRE(outline->outline.outline.spans.size() == 4);
}

TEST_CASE("export_board_outline round-trips an arc-cornered outline through "
          "import_board_outline within nanometre-scale tolerance",
          "[kicad][export]") {
  const StackupSnapshot stackup = two_layer_stackup();
  EntityId edge_cuts_id;
  stackup.table<Layer>().for_each([&](EntityId id, const Layer& layer) {
    if (layer.kind == LayerKind::EdgeCuts) {
      edge_cuts_id = id;
    }
  });

  // A quarter-circle arc corner (radius 3mm), the same shape
  // tests/corpus/kicad/rounded-rect-outline/board.kicad_pcb uses.
  const Point arc_start{.x = 0, .y = 3'000'000};
  const Point arc_end{.x = 3'000'000, .y = 0};
  const Point center{.x = 0, .y = 0};
  const Contour outline_contour{
      .spans = {Span{Arc{.start = arc_start,
                         .end = arc_end,
                         .center = center,
                         .direction = ArcDirection::Clockwise}},
                Span{Segment{.start = arc_end, .end = {.x = 3'000'000, .y = -10'000'000}}},
                Span{Segment{.start = {.x = 3'000'000, .y = -10'000'000},
                             .end = {.x = 0, .y = -10'000'000}}},
                Span{Segment{.start = {.x = 0, .y = -10'000'000}, .end = arc_start}}}};

  GeometryWorkspace geometry_workspace;
  geometry_workspace.insert(BoardOutline{
      .outline = Polygon{.outline = outline_contour, .holes = {}}, .layer = edge_cuts_id});
  const GeometrySnapshot original = geometry_workspace.commit();

  const std::vector<SExpr> nodes = export_board_outline(original, stackup);
  const SExpr layers_section = export_layers_section(stackup);
  const SExpr root = parse_sexpr(write_sexpr(wrap_board(layers_section, nodes)));
  const StackupSnapshot reimported_stackup = import_stackup(root);
  const GeometrySnapshot reimported = import_board_outline(root, reimported_stackup);

  const BoardOutline* result = nullptr;
  reimported.table<BoardOutline>().for_each(
      [&](EntityId, const BoardOutline& candidate) { result = &candidate; });
  REQUIRE(result != nullptr);
  REQUIRE(result->outline.outline.spans.size() == 4);

  const Arc* arc = find_arc_span(result->outline.outline.spans);
  REQUIRE(arc != nullptr);
  REQUIRE(std::abs(arc->center.x - center.x) <= 2);
  REQUIRE(std::abs(arc->center.y - center.y) <= 2);
}

TEST_CASE("export_board_outline throws when stackup has no Edge.Cuts layer", "[kicad][export]") {
  StackupWorkspace workspace;
  workspace.insert(copper_layer("F.Cu"));
  const GeometrySnapshot empty_geometry = GeometryWorkspace{}.commit();
  REQUIRE_THROWS_AS(export_board_outline(empty_geometry, workspace.commit()), ExportError);
}
