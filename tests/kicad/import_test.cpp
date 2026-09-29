// SPDX-License-Identifier: Apache-2.0
#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

#include <catch2/catch_test_macros.hpp>

using pcbir::BoardSnapshot;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::core::EntityId;
using pcbir::kicad::import_kicad_pcb;
using pcbir::kicad::import_stackup;
using pcbir::kicad::ImportError;
using pcbir::kicad::parse_sexpr;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::StackupSnapshot;

using pcbir::geometry::BoardOutline;
using pcbir::geometry::CopperPour;
using pcbir::geometry::Footprint;
using pcbir::geometry::Pad;
using pcbir::geometry::Track;
using pcbir::geometry::Via;

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

// A real 4-copper-layer board's (layers ...) section, taken verbatim from a
// pcbnew-scripted probe board (docs/rfcs/0003-kicad-importer-exporter.md).
// Note the numeric ids are NOT in physical order (B.Cu's id, 2, is lower
// than both inner layers') even though the file's own list order is.
constexpr const char* FOUR_LAYER_BOARD = R"(
  (kicad_pcb
    (version 20260206)
    (layers
      (0 "F.Cu" signal)
      (4 "In1.Cu" signal)
      (6 "In2.Cu" signal)
      (2 "B.Cu" signal)
      (5 "F.SilkS" user "F.Silkscreen")
      (7 "B.SilkS" user "B.Silkscreen")
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

// A small but complete board exercising every composable import_kicad_pcb
// pass at once (board outline, a footprint with one SMD pad and one
// thru-hole pad, a track, a free via, a zone, and 2 nets) -- enough to
// prove the orchestrator threads every pass's output into the next
// without colliding ids (pcbir/kicad/import.hpp), not a substitute for
// the per-pass fixtures each import_*_test.cpp already covers in depth.
constexpr const char* FULL_BOARD = R"(
  (kicad_pcb
    (version 20260206)
    (layers
      (0 "F.Cu" signal)
      (2 "B.Cu" signal)
      (25 "Edge.Cuts" user)
    )
    (gr_line (start 0 0) (end 20 0) (layer "Edge.Cuts"))
    (gr_line (start 20 0) (end 20 20) (layer "Edge.Cuts"))
    (gr_line (start 20 20) (end 0 20) (layer "Edge.Cuts"))
    (gr_line (start 0 20) (end 0 0) (layer "Edge.Cuts"))
    (footprint "" (layer "F.Cu") (at 5 5)
      (pad "1" smd rect (at 0 0) (size 1 1) (layers "F.Cu") (net "SIG1"))
      (pad "2" thru_hole circle (at 1 0) (size 1 1) (drill 0.5) (layers "*.Cu") (net "GND"))
    )
    (segment (start 5 5) (end 10 10) (width 0.25) (layer "F.Cu") (net "SIG1"))
    (via (at 15 15) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net "GND"))
    (zone (net "GND") (layer "B.Cu") (polygon (pts (xy 0 0) (xy 20 0) (xy 20 20) (xy 0 20))))
  )
)";

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] std::filesystem::path write_temp_kicad_pcb(const char* text, const char* filename) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / filename;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
  return path;
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

TEST_CASE("import_stackup's LayerStack lists inner Copper layers in physical (file) order, "
          "not numeric id order",
          "[kicad][import]") {
  // FOUR_LAYER_BOARD's ids are 0, 4, 6, 2 for F.Cu/In1.Cu/In2.Cu/B.Cu --
  // sorting by id would wrongly place B.Cu second, not last.
  const StackupSnapshot snapshot = import_stackup(parse_sexpr(FOUR_LAYER_BOARD));

  const LayerStack* stack = find_layer_stack(snapshot);
  REQUIRE(stack != nullptr);
  REQUIRE(stack->layers.size() == 4);

  const auto& layers = snapshot.table<Layer>();
  std::array<std::string, 4> names;
  for (std::size_t i = 0; i < stack->layers.size(); ++i) {
    const Layer* layer = layers.try_get(layers.find(stack->layers.at(i)));
    REQUIRE(layer != nullptr);
    names.at(i) = layer->name;
  }
  REQUIRE(names == std::array<std::string, 4>{"F.Cu", "In1.Cu", "In2.Cu", "B.Cu"});
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

TEST_CASE("import_kicad_pcb assembles every composable pass into one consistent BoardSnapshot",
          "[kicad][import]") {
  const std::filesystem::path path =
      write_temp_kicad_pcb(FULL_BOARD, "pcbir_import_kicad_pcb_full_test.kicad_pcb");
  const BoardSnapshot board = import_kicad_pcb(path);

  REQUIRE(board.geometry.table<BoardOutline>().size() == 1);
  REQUIRE(board.geometry.table<Footprint>().size() == 1);
  REQUIRE(board.geometry.table<Pad>().size() == 1);
  REQUIRE(board.geometry.table<Via>().size() == 2); // 1 thru-hole pad + 1 free via
  REQUIRE(board.geometry.table<Track>().size() == 1);
  REQUIRE(board.geometry.table<CopperPour>().size() == 1);
  REQUIRE(board.connectivity.table<Net>().size() == 2); // SIG1, GND
  REQUIRE(board.connectivity.table<Pin>().size() == 3); // 2 footprint pads + 1 free via
}

TEST_CASE("import_kicad_pcb's Footprint member ids each resolve to exactly one of the Pad/Via "
          "tables, never both",
          "[kicad][import]") {
  // The load-bearing regression check for the id-space-collision bug this
  // orchestrator exists to avoid (pcbir/kicad/import_geometry.hpp's own
  // doc comment): if two passes' entities ever collided on the same id,
  // a Footprint's own member id could spuriously resolve in *both* the
  // Pad and Via tables at once.
  const std::filesystem::path path =
      write_temp_kicad_pcb(FULL_BOARD, "pcbir_import_kicad_pcb_membership_test.kicad_pcb");
  const BoardSnapshot board = import_kicad_pcb(path);

  const Footprint* footprint = nullptr;
  board.geometry.table<Footprint>().for_each(
      [&](EntityId, const Footprint& candidate) { footprint = &candidate; });
  REQUIRE(footprint != nullptr);
  REQUIRE(footprint->pads.size() == 2);

  const auto& pads = board.geometry.table<Pad>();
  const auto& vias = board.geometry.table<Via>();
  bool every_member_resolves_exactly_once = true;
  for (const EntityId member_id : footprint->pads) {
    const bool is_pad = pads.try_get(pads.find(member_id)) != nullptr;
    const bool is_via = vias.try_get(vias.find(member_id)) != nullptr;
    every_member_resolves_exactly_once = every_member_resolves_exactly_once && (is_pad != is_via);
  }
  REQUIRE(every_member_resolves_exactly_once);
}

TEST_CASE("import_kicad_pcb throws ImportError on a KiCad version below the pinned minimum",
          "[kicad][import]") {
  const std::filesystem::path path = write_temp_kicad_pcb(
      R"((kicad_pcb (version 20211014) (layers (0 "F.Cu" signal) (2 "B.Cu" signal))))",
      "pcbir_import_kicad_pcb_old_version_test.kicad_pcb");
  REQUIRE_THROWS_AS(import_kicad_pcb(path), ImportError);
}

TEST_CASE("import_kicad_pcb throws ImportError when the file has no (kicad_pcb ...) root",
          "[kicad][import]") {
  const std::filesystem::path path =
      write_temp_kicad_pcb("(not_kicad_pcb)", "pcbir_import_kicad_pcb_wrong_root_test.kicad_pcb");
  REQUIRE_THROWS_AS(import_kicad_pcb(path), ImportError);
}

TEST_CASE("import_kicad_pcb throws ImportError when the file can't be opened", "[kicad][import]") {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "pcbir_import_kicad_pcb_does_not_exist.kicad_pcb";
  std::filesystem::remove(path);
  REQUIRE_THROWS_AS(import_kicad_pcb(path), ImportError);
}
