// SPDX-License-Identifier: Apache-2.0
//
// The optional, environment-gated half of RFC 0003's round-trip harness
// ("Verification against real KiCad"): where a real KiCad installation is
// available, actually load an exported file with pcbnew rather than
// trusting only this project's own importer to agree with itself
// (roundtrip_test.cpp's self-contained check). Not a CI requirement --
// none of this project's CI platforms has KiCad installed -- so this test
// SKIPs gracefully (reported, not silently ignored) when $KICAD_PYTHON
// isn't set, and is intended to be run locally wherever KiCad is present.
#include "pcbir/board_snapshot.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/export.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

using pcbir::BoardSnapshot;
using pcbir::core::EntityId;
using pcbir::kicad::export_kicad_pcb;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::StackupSnapshot;
using pcbir::stackup::StackupWorkspace;

using pcbir::geometry::BoardOutline;
using pcbir::geometry::Contour;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Polygon;
using pcbir::geometry::Segment;
using pcbir::geometry::Span;

namespace {

// A minimal but valid board: this check's job is verifying the exported
// file's *wrapper* structure (version/generator/general/paper/layers/setup)
// is something real pcbnew accepts, not re-exercising every entity type
// (roundtrip_test.cpp's self-contained check already covers that).
[[nodiscard]] BoardSnapshot minimal_board() {
  StackupWorkspace stackup_workspace;
  const auto front_handle = stackup_workspace.insert(Layer{.name = "F.Cu",
                                                           .kind = LayerKind::Copper,
                                                           .thickness_nm = 0,
                                                           .roughness_nm = 0,
                                                           .material = EntityId{}});
  const auto back_handle = stackup_workspace.insert(Layer{.name = "B.Cu",
                                                          .kind = LayerKind::Copper,
                                                          .thickness_nm = 0,
                                                          .roughness_nm = 0,
                                                          .material = EntityId{}});
  const auto edge_cuts_handle = stackup_workspace.insert(Layer{.name = "Edge.Cuts",
                                                               .kind = LayerKind::EdgeCuts,
                                                               .thickness_nm = 0,
                                                               .roughness_nm = 0,
                                                               .material = EntityId{}});
  const StackupSnapshot stackup_with_layers = stackup_workspace.commit();
  const auto& layers = stackup_with_layers.table<Layer>();
  // export_layers_section only emits Copper rows for layers listed in the
  // LayerStack (pcbir/kicad/export.hpp), not for every Copper-kind Layer
  // in the table -- a board with no LayerStack exports zero copper layers,
  // which real pcbnew rejects (verified on this device).
  stackup_workspace.insert(
      LayerStack{.name = "", .layers = {layers.id_of(front_handle), layers.id_of(back_handle)}});
  const auto stackup_committed = stackup_workspace.commit();
  const EntityId edge_cuts = stackup_committed.table<Layer>().id_of(edge_cuts_handle);

  constexpr int64_t mm = 1'000'000;
  GeometryWorkspace geometry_workspace;
  geometry_workspace
      .insert(
          BoardOutline{
              .outline =
                  Polygon{
                      .outline =
                          Contour{
                              .spans =
                                  {
                                      Span{
                                          Segment{.start = {.x = 0, .y = 0},
                                                  .end = {.x = 20 * mm, .y = 0}}},
                                      Span{
                                          Segment{.start = {.x = 20 * mm, .y = 0},
                                                  .end = {.x = 20 * mm, .y = 20 * mm}}},
                                      Span{
                                          Segment{.start = {.x = 20 * mm, .y = 20 * mm},
                                                  .end = {.x = 0, .y = 20 * mm}}},
                                      Span{
                                          Segment{.start = {.x = 0, .y = 20 * mm},
                                                  .end = {.x = 0, .y = 0}}},
                                  },
                          },
                      .holes = {},
                  },
              .layer = edge_cuts});

  BoardSnapshot board;
  board.stackup = stackup_committed;
  board.geometry = geometry_workspace.commit();
  return board;
}

} // namespace

TEST_CASE("export_kicad_pcb output reopens in real KiCad (pcbnew), when available",
          "[kicad][roundtrip][pcbnew]") {
  const char* kicad_python = std::getenv("KICAD_PYTHON");
  if (kicad_python == nullptr) {
    SKIP("KICAD_PYTHON not set; skipping real-pcbnew verification (see "
         "docs/rfcs/0003-kicad-importer-exporter.md -- \"Verification against real KiCad\")");
  }

  const std::filesystem::path board_path =
      std::filesystem::temp_directory_path() / "pcbir_pcbnew_verify_test.kicad_pcb";
  export_kicad_pcb(minimal_board(), board_path);

  const std::filesystem::path script_path =
      std::filesystem::path(PCBIR_TEST_KICAD_DIR) / "verify_with_pcbnew.py";
  const std::string command = "\"" + std::string(kicad_python) + "\" \"" + script_path.string() +
                              "\" \"" + board_path.string() + "\"";
  // NOLINTNEXTLINE(cert-env33-c,concurrency-mt-unsafe,bugprone-command-processor)
  const int result = std::system(command.c_str());
  REQUIRE(result == 0);
}
