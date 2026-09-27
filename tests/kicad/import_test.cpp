// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/entity_id.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <catch2/catch_test_macros.hpp>

using pcbir::core::EntityId;
using pcbir::kicad::import_stackup;
using pcbir::kicad::ImportError;
using pcbir::kicad::parse_sexpr;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::StackupSnapshot;

namespace {

// A real 2-layer board's (layers ...) section, taken verbatim from a
// pcbnew-scripted probe board (docs/rfcs/0003-kicad-importer-exporter.md).
// 9 rows total: 2 Copper, 1 Edge.Cuts, 2 Silkscreen, and 4 with no
// recognized LayerKind yet (F.Adhes/B.Adhes/F.Paste/F.Mask).
constexpr const char* TWO_LAYER_BOARD = R"(
  (kicad_pcb
    (version 20260206)
    (layers
      (0 "F.Cu" signal)
      (2 "B.Cu" signal)
      (9 "F.Adhes" user "F.Adhesive")
      (11 "B.Adhes" user "B.Adhesive")
      (13 "F.Paste" user)
      (5 "F.SilkS" user "F.Silkscreen")
      (7 "B.SilkS" user "B.Silkscreen")
      (1 "F.Mask" user)
      (25 "Edge.Cuts" user)
    )
  )
)";

[[nodiscard]] const LayerStack* find_layer_stack(const StackupSnapshot& snapshot) {
  const LayerStack* result = nullptr;
  snapshot.table<LayerStack>().for_each(
      [&](EntityId, const LayerStack& candidate) { result = &candidate; });
  return result;
}

} // namespace

TEST_CASE("import_stackup imports Copper layers in KiCad's numeric id order", "[kicad][import]") {
  const StackupSnapshot snapshot = import_stackup(parse_sexpr(TWO_LAYER_BOARD));

  const auto& layers = snapshot.table<Layer>();
  const Layer* front_copper = nullptr;
  const Layer* back_copper = nullptr;
  int copper_count = 0;
  layers.for_each([&](EntityId, const Layer& layer) {
    if (layer.kind == LayerKind::Copper) {
      ++copper_count;
      if (layer.name == "F.Cu") {
        front_copper = &layer;
      } else if (layer.name == "B.Cu") {
        back_copper = &layer;
      }
    }
  });

  REQUIRE(copper_count == 2);
  REQUIRE(front_copper != nullptr);
  REQUIRE(back_copper != nullptr);
  REQUIRE(front_copper->thickness_nm > 0);
}

TEST_CASE("import_stackup builds exactly one LayerStack containing both Copper layers",
          "[kicad][import]") {
  const StackupSnapshot snapshot = import_stackup(parse_sexpr(TWO_LAYER_BOARD));

  REQUIRE(snapshot.table<LayerStack>().size() == 1);
  const LayerStack* stack = find_layer_stack(snapshot);
  REQUIRE(stack != nullptr);
  REQUIRE(stack->layers.size() == 2);
}

TEST_CASE("import_stackup's LayerStack lists Copper layers in KiCad's id order",
          "[kicad][import]") {
  const StackupSnapshot snapshot = import_stackup(parse_sexpr(TWO_LAYER_BOARD));

  const LayerStack* stack = find_layer_stack(snapshot);
  REQUIRE(stack != nullptr);
  REQUIRE(stack->layers.size() == 2);

  const auto& layers = snapshot.table<Layer>();
  const Layer* first = layers.try_get(layers.find(stack->layers.at(0)));
  const Layer* second = layers.try_get(layers.find(stack->layers.at(1)));
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  REQUIRE(first->name == "F.Cu");
  REQUIRE(second->name == "B.Cu");
}

TEST_CASE("import_stackup imports Edge.Cuts as a non-physical EdgeCuts layer", "[kicad][import]") {
  const StackupSnapshot snapshot = import_stackup(parse_sexpr(TWO_LAYER_BOARD));

  const auto& layers = snapshot.table<Layer>();
  const Layer* edge_cuts = nullptr;
  layers.for_each([&](EntityId, const Layer& layer) {
    if (layer.kind == LayerKind::EdgeCuts) {
      edge_cuts = &layer;
    }
  });
  REQUIRE(edge_cuts != nullptr);
  REQUIRE(edge_cuts->name == "Edge.Cuts");
  REQUIRE(edge_cuts->thickness_nm == 0);
}

TEST_CASE("import_stackup's LayerStack excludes the EdgeCuts layer", "[kicad][import]") {
  const StackupSnapshot snapshot = import_stackup(parse_sexpr(TWO_LAYER_BOARD));

  const LayerStack* stack = find_layer_stack(snapshot);
  REQUIRE(stack != nullptr);

  const auto& layers = snapshot.table<Layer>();
  bool stack_excludes_edge_cuts = true;
  for (const EntityId& member : stack->layers) {
    const Layer* member_layer = layers.try_get(layers.find(member));
    if (member_layer == nullptr || member_layer->kind == LayerKind::EdgeCuts) {
      stack_excludes_edge_cuts = false;
    }
  }
  REQUIRE(stack_excludes_edge_cuts);
}

TEST_CASE("import_stackup imports F.SilkS/B.SilkS as non-physical Silkscreen layers",
          "[kicad][import]") {
  const StackupSnapshot snapshot = import_stackup(parse_sexpr(TWO_LAYER_BOARD));

  const auto& layers = snapshot.table<Layer>();
  int silkscreen_count = 0;
  bool all_recognized_sides = true;
  layers.for_each([&](EntityId, const Layer& layer) {
    if (layer.kind == LayerKind::Silkscreen) {
      ++silkscreen_count;
      if (layer.name != "F.SilkS" && layer.name != "B.SilkS") {
        all_recognized_sides = false;
      }
    }
  });
  REQUIRE(silkscreen_count == 2);
  REQUIRE(all_recognized_sides);
}

TEST_CASE("import_stackup skips layers with no known LayerKind", "[kicad][import]") {
  const StackupSnapshot snapshot = import_stackup(parse_sexpr(TWO_LAYER_BOARD));

  // 2 Copper + 1 EdgeCuts + 2 Silkscreen == 5; the remaining 4 rows
  // (F.Adhes/B.Adhes/F.Paste/F.Mask) have no LayerKind yet and must not
  // appear at all.
  REQUIRE(snapshot.table<Layer>().size() == 5);
}

TEST_CASE("import_stackup throws ImportError when (layers ...) is missing", "[kicad][import]") {
  const pcbir::kicad::SExpr root = parse_sexpr("(kicad_pcb (version 20260206))");
  REQUIRE_THROWS_AS(import_stackup(root), ImportError);
}

TEST_CASE("import_stackup throws ImportError on a malformed layer row", "[kicad][import]") {
  const pcbir::kicad::SExpr root = parse_sexpr(R"((kicad_pcb (layers (0))))");
  REQUIRE_THROWS_AS(import_stackup(root), ImportError);
}

TEST_CASE("import_stackup produces no LayerStack when no Copper layer is present",
          "[kicad][import]") {
  const pcbir::kicad::SExpr root = parse_sexpr(R"((kicad_pcb (layers (25 "Edge.Cuts" user))))");
  const StackupSnapshot snapshot = import_stackup(root);

  REQUIRE(snapshot.table<LayerStack>().size() == 0);
}
