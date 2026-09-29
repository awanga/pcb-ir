// SPDX-License-Identifier: Apache-2.0
//
// Exercises FidelityReport generation (pcbir/kicad/fidelity.hpp) via
// import_kicad_pcb/export_kicad_pcb -- independent of import_test.cpp/
// export_test.cpp's own coverage of those orchestrators' core assembly
// logic.
#include "pcbir/board_snapshot.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/keepout.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/export.hpp"
#include "pcbir/kicad/fidelity.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

#include <catch2/catch_test_macros.hpp>

using pcbir::BoardSnapshot;
using pcbir::kicad::export_kicad_pcb;
using pcbir::kicad::FidelityRecord;
using pcbir::kicad::FidelityReport;
using pcbir::kicad::FidelityTier;
using pcbir::kicad::import_kicad_pcb;

using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Keepout;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::StackupWorkspace;

namespace {

// A footprint with an axis-aligned (0-degree) rect SMD pad and a thru-hole
// pad, a board outline, a track, a free via, and a zone -- every in-scope
// entity type at once, all fully classifiable.
constexpr const char* FIDELITY_BOARD = R"(
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

// Same board, but the footprint carries a non-90-degree rotation and its
// SMD pad is `custom`-shaped -- exercises the two content-dependent
// Approximated classifications (rotation, pad shape).
constexpr const char* ROTATED_CUSTOM_PAD_BOARD = R"(
  (kicad_pcb
    (version 20260206)
    (layers (0 "F.Cu" signal) (2 "B.Cu" signal) (25 "Edge.Cuts" user))
    (footprint "" (layer "F.Cu") (at 5 5 30)
      (pad "1" smd custom (at 0 0) (size 0.5 0.5) (layers "F.Cu")
        (primitives
          (gr_poly (pts (xy -0.5 -0.5) (xy 0.5 -0.5) (xy 0.5 0.5) (xy -0.5 0.5)) (width 0)))))
  )
)";

// Two distinct unrecognized top-level sections, one repeated -- verifies
// distinct-tag deduplication as well as detection itself.
constexpr const char* BOARD_WITH_UNKNOWN_SECTION = R"(
  (kicad_pcb
    (version 20260206)
    (layers (0 "F.Cu" signal) (25 "Edge.Cuts" user))
    (dimension (type aligned) (layer "F.Cu"))
    (dimension (type aligned) (layer "F.Cu"))
    (group (name "G1") (members))
  )
)";

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] std::filesystem::path write_temp_kicad_pcb(const char* text, const char* filename) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / filename;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
  return path;
}

[[nodiscard]] const FidelityRecord* find_record(const FidelityReport& report,
                                                const std::string& entity_kind,
                                                const std::string& attribute) {
  const auto it = std::ranges::find_if(report.records, [&](const FidelityRecord& record) {
    return record.entity_kind == entity_kind && record.attribute == attribute;
  });
  return it != report.records.end() ? &*it : nullptr;
}

[[nodiscard]] int count_tier(const FidelityReport& report, FidelityTier tier) {
  return static_cast<int>(std::ranges::count_if(
      report.records, [&](const FidelityRecord& record) { return record.tier == tier; }));
}

} // namespace

TEST_CASE("import_kicad_pcb's fidelity report is Preserved for every in-scope entity except "
          "the two known unconditional approximations",
          "[kicad][fidelity]") {
  const std::filesystem::path path =
      write_temp_kicad_pcb(FIDELITY_BOARD, "pcbir_fidelity_import_test.kicad_pcb");
  FidelityReport report;
  const BoardSnapshot board = import_kicad_pcb(path, &report);
  REQUIRE_FALSE(report.records.empty());
  REQUIRE(count_tier(report, FidelityTier::Unsupported) == 0);
  REQUIRE(count_tier(report, FidelityTier::Lost) == 0);

  const FidelityRecord* thickness = find_record(report, "layer", "thickness");
  REQUIRE(thickness != nullptr);
  REQUIRE(thickness->tier == FidelityTier::Approximated);

  const FidelityRecord* via_diameter = find_record(report, "via", "finished_hole_diameter");
  REQUIRE(via_diameter != nullptr);
  REQUIRE(via_diameter->tier == FidelityTier::Approximated);

  // Every other record (board_outline, footprint rotation, pad shape,
  // track, zone) is Preserved for this fully in-scope, axis-aligned,
  // non-custom-pad board. The 4 Approximated records are: F.Cu/B.Cu
  // thickness (2 Copper layers) and finished-hole-diameter for the 2 Via
  // entities (FIDELITY_BOARD's thru-hole pad is itself modeled as a Via,
  // plus the one free via).
  const int approximated = count_tier(report, FidelityTier::Approximated);
  REQUIRE(approximated == 4);

  (void)board;
}

TEST_CASE("import_kicad_pcb reports a non-90-degree footprint rotation and a custom-shape pad "
          "as Approximated",
          "[kicad][fidelity]") {
  const std::filesystem::path path = write_temp_kicad_pcb(
      ROTATED_CUSTOM_PAD_BOARD, "pcbir_fidelity_rotated_custom_pad_test.kicad_pcb");
  FidelityReport report;
  const BoardSnapshot board = import_kicad_pcb(path, &report);
  (void)board;

  const FidelityRecord* rotation = find_record(report, "footprint", "rotation");
  REQUIRE(rotation != nullptr);
  REQUIRE(rotation->tier == FidelityTier::Approximated);

  const FidelityRecord* pad_shape = find_record(report, "pad", "shape");
  REQUIRE(pad_shape != nullptr);
  REQUIRE(pad_shape->tier == FidelityTier::Approximated);
}

TEST_CASE("import_kicad_pcb reports an unrecognized top-level KiCad section as Unsupported, "
          "once per distinct tag",
          "[kicad][fidelity]") {
  const std::filesystem::path path = write_temp_kicad_pcb(
      BOARD_WITH_UNKNOWN_SECTION, "pcbir_fidelity_unknown_section_test.kicad_pcb");
  FidelityReport report;
  const BoardSnapshot board = import_kicad_pcb(path, &report);
  (void)board;

  const FidelityRecord* dimension = find_record(report, "dimension", "presence");
  REQUIRE(dimension != nullptr);
  REQUIRE(dimension->tier == FidelityTier::Unsupported);

  const FidelityRecord* group = find_record(report, "group", "presence");
  REQUIRE(group != nullptr);
  REQUIRE(group->tier == FidelityTier::Unsupported);

  // Two (dimension ...) entries collapse into exactly one record.
  const int dimension_records =
      static_cast<int>(std::ranges::count_if(report.records, [](const FidelityRecord& record) {
        return record.entity_kind == "dimension";
      }));
  REQUIRE(dimension_records == 1);
}

TEST_CASE("export_kicad_pcb's fidelity report mirrors the same pad-shape/rotation/via "
          "classifications as import",
          "[kicad][fidelity]") {
  const std::filesystem::path source_path =
      write_temp_kicad_pcb(FIDELITY_BOARD, "pcbir_fidelity_export_source_test.kicad_pcb");
  const BoardSnapshot board = import_kicad_pcb(source_path);

  const std::filesystem::path export_path =
      std::filesystem::temp_directory_path() / "pcbir_fidelity_export_test.kicad_pcb";
  FidelityReport report;
  export_kicad_pcb(board, export_path, &report);

  const FidelityRecord* pad_shape = find_record(report, "pad", "shape");
  REQUIRE(pad_shape != nullptr);
  REQUIRE(pad_shape->tier == FidelityTier::Preserved); // FIDELITY_BOARD's pad has an extension

  const FidelityRecord* via_diameter = find_record(report, "via", "finished_hole_diameter");
  REQUIRE(via_diameter != nullptr);
  REQUIRE(via_diameter->tier == FidelityTier::Approximated);

  REQUIRE(count_tier(report, FidelityTier::Unsupported) == 0);
}

TEST_CASE("export_kicad_pcb reports a non-empty unexported component table as Unsupported",
          "[kicad][fidelity]") {
  StackupWorkspace stackup_workspace;
  stackup_workspace.insert(Layer{.name = "Edge.Cuts",
                                 .kind = LayerKind::EdgeCuts,
                                 .thickness_nm = 0,
                                 .roughness_nm = 0,
                                 .material = pcbir::core::EntityId{}});

  BoardSnapshot board;
  board.stackup = stackup_workspace.commit();
  GeometryWorkspace geometry_workspace;
  geometry_workspace.insert(
      Keepout{.outline = pcbir::geometry::Polygon{}, .layer = pcbir::core::EntityId{}});
  board.geometry = geometry_workspace.commit();

  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "pcbir_fidelity_unexported_component_test.kicad_pcb";
  FidelityReport report;
  export_kicad_pcb(board, path, &report);

  const FidelityRecord* keepout = find_record(report, "keepout", "presence");
  REQUIRE(keepout != nullptr);
  REQUIRE(keepout->tier == FidelityTier::Unsupported);
}
