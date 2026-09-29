// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/import_geometry.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cmath>
#include <variant>

#include <catch2/catch_test_macros.hpp>

using pcbir::core::EntityId;
using pcbir::kicad::import_board_outline;
using pcbir::kicad::import_stackup;
using pcbir::kicad::ImportError;
using pcbir::kicad::parse_sexpr;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::StackupSnapshot;

using pcbir::geometry::Arc;
using pcbir::geometry::BoardOutline;
using pcbir::geometry::GeometrySnapshot;

namespace {

// A rectangular board outline (Edge.Cuts) authored out of traversal order
// and with mixed segment directions, mirroring what a real KiCad file
// looks like -- pcbnew doesn't guarantee edges are written in loop order.
constexpr const char* RECT_BOARD = R"(
  (kicad_pcb
    (layers
      (0 "F.Cu" signal)
      (2 "B.Cu" signal)
      (25 "Edge.Cuts" user)
    )
    (gr_line (start 50 0) (end 50 30) (layer "Edge.Cuts"))
    (gr_line (start 0 0) (end 50 0) (layer "Edge.Cuts"))
    (gr_line (start 50 30) (end 0 30) (layer "Edge.Cuts"))
    (gr_line (start 0 30) (end 0 0) (layer "Edge.Cuts"))
    (gr_line (start 5 5) (end 5 6) (layer "F.SilkS"))
  )
)";

// A rounded-corner outline exercising gr_arc: 3 straight edges plus one
// 90-degree arc corner, matching a probe verified against real pcbnew
// output (docs/rfcs/0003-kicad-importer-exporter.md).
constexpr const char* ROUNDED_CORNER_BOARD = R"(
  (kicad_pcb
    (layers
      (0 "F.Cu" signal)
      (25 "Edge.Cuts" user)
    )
    (gr_line (start 5 0) (end 50 0) (layer "Edge.Cuts"))
    (gr_line (start 50 0) (end 50 25) (layer "Edge.Cuts"))
    (gr_line (start 50 25) (end 0 25) (layer "Edge.Cuts"))
    (gr_line (start 0 25) (end 0 5) (layer "Edge.Cuts"))
    (gr_arc (start 0 5) (mid 1.464466 1.464466) (end 5 0) (layer "Edge.Cuts"))
  )
)";

// An outline with a dangling edge (no edge closes the loop back to the
// first segment's start).
constexpr const char* OPEN_BOARD = R"(
  (kicad_pcb
    (layers (25 "Edge.Cuts" user))
    (gr_line (start 0 0) (end 10 0) (layer "Edge.Cuts"))
    (gr_line (start 10 0) (end 10 10) (layer "Edge.Cuts"))
  )
)";

[[nodiscard]] GeometrySnapshot import_outline(const char* board_text) {
  const pcbir::kicad::SExpr root = parse_sexpr(board_text);
  const StackupSnapshot stackup = import_stackup(root);
  return import_board_outline(root, stackup);
}

} // namespace

TEST_CASE("import_board_outline assembles disjoint edges into one closed BoardOutline",
          "[kicad][import]") {
  const GeometrySnapshot snapshot = import_outline(RECT_BOARD);

  REQUIRE(snapshot.table<BoardOutline>().size() == 1);
}

TEST_CASE("import_board_outline's BoardOutline references the Edge.Cuts layer", "[kicad][import]") {
  const pcbir::kicad::SExpr root = parse_sexpr(RECT_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const GeometrySnapshot snapshot = import_board_outline(root, stackup);

  EntityId edge_cuts_id;
  stackup.table<Layer>().for_each([&](EntityId id, const Layer& layer) {
    if (layer.kind == LayerKind::EdgeCuts) {
      edge_cuts_id = id;
    }
  });
  REQUIRE_FALSE(edge_cuts_id.is_null());

  const BoardOutline* outline = nullptr;
  snapshot.table<BoardOutline>().for_each(
      [&](EntityId, const BoardOutline& candidate) { outline = &candidate; });
  REQUIRE(outline != nullptr);
  REQUIRE(outline->layer == edge_cuts_id);
}

TEST_CASE("import_board_outline's outline has 4 spans forming a closed rectangle",
          "[kicad][import]") {
  const GeometrySnapshot snapshot = import_outline(RECT_BOARD);

  const BoardOutline* outline = nullptr;
  snapshot.table<BoardOutline>().for_each(
      [&](EntityId, const BoardOutline& candidate) { outline = &candidate; });
  REQUIRE(outline != nullptr);
  REQUIRE(outline->outline.outline.spans.size() == 4);
  REQUIRE(outline->outline.holes.empty());
}

TEST_CASE("import_board_outline canonicalizes the outline to counterclockwise winding",
          "[kicad][import]") {
  const GeometrySnapshot snapshot = import_outline(RECT_BOARD);

  const BoardOutline* outline = nullptr;
  snapshot.table<BoardOutline>().for_each(
      [&](EntityId, const BoardOutline& candidate) { outline = &candidate; });
  REQUIRE(outline != nullptr);
  REQUIRE(pcbir::geometry::orientation(outline->outline.outline) ==
          pcbir::geometry::Orientation::CounterClockwise);
}

TEST_CASE("import_board_outline converts a gr_arc into an exact-radius Arc span",
          "[kicad][import]") {
  const GeometrySnapshot snapshot = import_outline(ROUNDED_CORNER_BOARD);

  const BoardOutline* outline = nullptr;
  snapshot.table<BoardOutline>().for_each(
      [&](EntityId, const BoardOutline& candidate) { outline = &candidate; });
  REQUIRE(outline != nullptr);
  REQUIRE(outline->outline.outline.spans.size() == 5);

  const auto& spans = outline->outline.outline.spans;
  const auto it = std::ranges::find_if(
      spans, [](const auto& span) { return std::holds_alternative<Arc>(span); });
  REQUIRE(it != spans.end());

  const Arc& arc = std::get<Arc>(*it);
  REQUIRE(arc.is_valid());
  // Center should land very close to (5mm, 5mm) -- exact up to the
  // floating-point circumcenter's rounding (import_geometry.hpp).
  REQUIRE(std::abs(arc.center.x - 5'000'000) <= 2);
  REQUIRE(std::abs(arc.center.y - 5'000'000) <= 2);
}

TEST_CASE("import_board_outline throws ImportError on a dangling edge", "[kicad][import]") {
  const pcbir::kicad::SExpr root = parse_sexpr(OPEN_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  REQUIRE_THROWS_AS(import_board_outline(root, stackup), ImportError);
}

TEST_CASE("import_board_outline throws ImportError when the stackup has no Edge.Cuts layer",
          "[kicad][import]") {
  const pcbir::kicad::SExpr root = parse_sexpr(R"((kicad_pcb (layers (0 "F.Cu" signal))))");
  const StackupSnapshot stackup = import_stackup(root);
  REQUIRE_THROWS_AS(import_board_outline(root, stackup), ImportError);
}

TEST_CASE("import_board_outline produces no BoardOutline when there is no Edge.Cuts geometry",
          "[kicad][import]") {
  const pcbir::kicad::SExpr root =
      parse_sexpr(R"((kicad_pcb (layers (0 "F.Cu" signal) (25 "Edge.Cuts" user))))");
  const StackupSnapshot stackup = import_stackup(root);
  const GeometrySnapshot snapshot = import_board_outline(root, stackup);

  REQUIRE(snapshot.table<BoardOutline>().size() == 0);
}

TEST_CASE("import_board_outline given a base snapshot continues its id space instead of "
          "restarting at 1",
          "[kicad][import]") {
  // Mirrors how import_kicad_pcb chains composable passes together
  // (pcbir/kicad/import.hpp): calling a pass a second time with the first
  // call's result as `base` must not reuse any id the first call already
  // handed out.
  const pcbir::kicad::SExpr root = parse_sexpr(RECT_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const GeometrySnapshot first = import_board_outline(root, stackup);
  const GeometrySnapshot second = import_board_outline(root, stackup, first);

  REQUIRE(first.table<BoardOutline>().size() == 1);
  REQUIRE(second.table<BoardOutline>().size() == 2);

  EntityId first_id;
  first.table<BoardOutline>().for_each([&](EntityId id, const BoardOutline&) { first_id = id; });

  bool second_call_id_present = false;
  bool no_collision = true;
  second.table<BoardOutline>().for_each([&](EntityId id, const BoardOutline&) {
    if (id == first_id) {
      second_call_id_present = true;
    } else {
      no_collision = no_collision && (id != first_id);
    }
  });
  REQUIRE(second_call_id_present); // the base's own BoardOutline carried forward unchanged
  REQUIRE(no_collision);           // the new BoardOutline got a fresh, non-colliding id
}
