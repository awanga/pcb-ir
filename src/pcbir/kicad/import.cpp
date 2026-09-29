// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import.hpp"

#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/entity_domain.hpp"
#include "pcbir/extension.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/fidelity.hpp"
#include "pcbir/kicad/import_footprint.hpp"
#include "pcbir/kicad/import_geometry.hpp"
#include "pcbir/kicad/import_nets.hpp"
#include "pcbir/kicad/import_track.hpp"
#include "pcbir/kicad/import_via.hpp"
#include "pcbir/kicad/import_zone.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "pad_shape_extension.hpp"
#include "sexpr_util.hpp"

namespace pcbir::kicad {

namespace {

// See import.hpp -- KiCad's `(layers ...)` carries no thickness/material
// data; this is the single most common real-board value (1oz copper).
constexpr int64_t DEFAULT_COPPER_THICKNESS_NM = 35000;

// One row of KiCad's `(layers ...)` list: `(id "name" type ["friendly"])`.
// `id` is not itself tagged (unlike every other KiCad section), so this is
// parsed positionally rather than via find_child().
struct KicadLayerRow {
  int64_t id = 0;
  std::string name;
};

[[nodiscard]] KicadLayerRow parse_layer_row(const SExpr& row) {
  if (row.children.size() < 2 || !row.children.at(0).is_symbol() ||
      !row.children.at(1).is_string()) {
    throw ImportError("malformed (layers ...) entry: expected (id \"name\" type ...)");
  }
  return KicadLayerRow{.id = parse_i64(row.children.at(0)), .name = row.children.at(1).text};
}

[[nodiscard]] bool is_copper(const std::string& layer_name) {
  return layer_name.ends_with(".Cu");
}

[[nodiscard]] bool is_silkscreen(const std::string& layer_name) {
  return layer_name == "F.SilkS" || layer_name == "B.SilkS";
}

} // namespace

namespace {

// The minimum KiCad board-format version (KiCad's own field, not PCB-IR's
// FormatVersion) import_kicad_pcb accepts -- see import.hpp's own doc
// comment and docs/rfcs/0003-kicad-importer-exporter.md's "Target version
// pin". Below this, the file predates the version this importer was
// verified against real pcbnew output for, and is rejected with a
// diagnostic rather than attempted with best-effort parsing.
constexpr int64_t MIN_SUPPORTED_KICAD_VERSION = 20260206;

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  const std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw ImportError("failed to open '" + path.string() + "'");
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void validate_kicad_pcb_root(const SExpr& root) {
  if (!root.is_list() || root.children.empty() || !root.children.front().is_symbol() ||
      root.children.front().text != "kicad_pcb") {
    throw ImportError("not a valid .kicad_pcb file: expected a top-level (kicad_pcb ...) list");
  }
  const SExpr* version_node = find_child(root, "version");
  if (version_node == nullptr || version_node->children.size() < 2) {
    throw ImportError("malformed .kicad_pcb file: missing (version ...)");
  }
  const int64_t version = parse_i64(version_node->children.at(1));
  if (version < MIN_SUPPORTED_KICAD_VERSION) {
    throw ImportError("unsupported .kicad_pcb version " + std::to_string(version) +
                      "; this importer requires >= " + std::to_string(MIN_SUPPORTED_KICAD_VERSION) +
                      " (KiCad 10)");
  }
}

} // namespace

namespace {

constexpr int64_t DEGREES_E6_PER_90 = 90'000'000;

[[nodiscard]] bool is_axis_aligned(int64_t rotation_e6) {
  return rotation_e6 % DEGREES_E6_PER_90 == 0;
}

void add_record(FidelityReport& report,
                EntityDomain domain,
                core::EntityId entity_id,
                std::string entity_kind,
                std::string attribute,
                FidelityTier tier,
                std::string reason) {
  report.records.push_back(FidelityRecord{.domain = domain,
                                          .entity_id = entity_id,
                                          .entity_kind = std::move(entity_kind),
                                          .attribute = std::move(attribute),
                                          .tier = tier,
                                          .reason = std::move(reason)});
}

void populate_stackup_fidelity(FidelityReport& report, const stackup::StackupSnapshot& stackup) {
  stackup.table<stackup::Layer>().for_each([&](core::EntityId id, const stackup::Layer& layer) {
    if (layer.kind == stackup::LayerKind::Copper) {
      add_record(report,
                 EntityDomain::Stackup,
                 id,
                 "layer",
                 "thickness",
                 FidelityTier::Approximated,
                 "KiCad's (layers ...) carries no per-layer thickness; a fixed default was "
                 "applied");
    } else {
      add_record(report,
                 EntityDomain::Stackup,
                 id,
                 "layer",
                 "kind",
                 FidelityTier::Preserved,
                 "layer kind/name round-trip exactly");
    }
  });
}

// A Pad id has a matching PCBIR_KICAD/pad_shape entry in `extensions`.
[[nodiscard]] std::set<core::EntityId::ValueType>
pad_ids_with_shape_extension(const std::vector<Extension>& extensions) {
  std::set<core::EntityId::ValueType> ids;
  for (const Extension& extension : extensions) {
    if (extension.domain == EntityDomain::Geometry &&
        extension.ext_namespace == PAD_SHAPE_EXTENSION_NAMESPACE &&
        extension.name == PAD_SHAPE_EXTENSION_NAME) {
      ids.insert(extension.entity_id.value());
    }
  }
  return ids;
}

void populate_geometry_fidelity(FidelityReport& report,
                                const geometry::GeometrySnapshot& geometry,
                                const std::vector<Extension>& extensions) {
  geometry.table<geometry::BoardOutline>().for_each(
      [&](core::EntityId id, const geometry::BoardOutline&) {
        add_record(report,
                   EntityDomain::Geometry,
                   id,
                   "board_outline",
                   "outline",
                   FidelityTier::Preserved,
                   "straight/arc outline geometry round-trips exactly");
      });

  geometry.table<geometry::Footprint>().for_each(
      [&](core::EntityId id, const geometry::Footprint& footprint) {
        const bool axis_aligned = is_axis_aligned(footprint.rotation_e6);
        add_record(report,
                   EntityDomain::Geometry,
                   id,
                   "footprint",
                   "rotation",
                   axis_aligned ? FidelityTier::Preserved : FidelityTier::Approximated,
                   axis_aligned ? "90-degree-multiple rotation is exact"
                                : "non-90-degree rotation uses geometry::rotate's documented "
                                  "floating-point tolerance");
      });

  const std::set<core::EntityId::ValueType> pad_shape_ids =
      pad_ids_with_shape_extension(extensions);
  geometry.table<geometry::Pad>().for_each([&](core::EntityId id, const geometry::Pad&) {
    const bool preserved = pad_shape_ids.contains(id.value());
    add_record(report,
               EntityDomain::Geometry,
               id,
               "pad",
               "shape",
               preserved ? FidelityTier::Preserved : FidelityTier::Approximated,
               preserved ? "PCBIR_KICAD/pad_shape extension carries the original primitive"
                         : "no PCBIR_KICAD/pad_shape extension; shape identity lost, copper "
                           "geometry preserved");
  });

  geometry.table<geometry::Via>().for_each([&](core::EntityId id, const geometry::Via&) {
    add_record(report,
               EntityDomain::Geometry,
               id,
               "via",
               "finished_hole_diameter",
               FidelityTier::Approximated,
               "KiCad's per-pad (drill ...) has no separate finished-hole-diameter field; "
               "approximated as equal to the drill diameter");
  });

  geometry.table<geometry::Track>().for_each([&](core::EntityId id, const geometry::Track&) {
    add_record(report,
               EntityDomain::Geometry,
               id,
               "track",
               "path",
               FidelityTier::Preserved,
               "segment/arc path geometry round-trips exactly");
  });

  geometry.table<geometry::CopperPour>().for_each([&](core::EntityId id,
                                                      const geometry::CopperPour&) {
    add_record(report,
               EntityDomain::Geometry,
               id,
               "zone",
               "outline",
               FidelityTier::Preserved,
               "authored outline round-trips exactly (computed fill polygons are out of scope)");
  });
}

// KiCad top-level sections this importer actually consumes -- anything
// else found as a direct child of the root `(kicad_pcb ...)` list is a
// section this pass doesn't recognize at all, reported Unsupported rather
// than silently ignored (docs/rfcs/0003-kicad-importer-exporter.md's
// "Explicitly out of scope").
[[nodiscard]] bool is_recognized_top_level_tag(const std::string& tag) {
  static const std::set<std::string> recognized{"version",
                                                "generator",
                                                "generator_version",
                                                "general",
                                                "paper",
                                                "layers",
                                                "setup",
                                                "footprint",
                                                "gr_line",
                                                "gr_arc",
                                                "segment",
                                                "arc",
                                                "via",
                                                "zone",
                                                "embedded_fonts"};
  return recognized.contains(tag);
}

void populate_unsupported_sections_fidelity(FidelityReport& report, const SExpr& kicad_pcb) {
  std::set<std::string> unrecognized_tags;
  for (const SExpr& child : kicad_pcb.children) {
    if (child.is_list() && !child.children.empty() && child.children.front().is_symbol() &&
        !is_recognized_top_level_tag(child.children.front().text)) {
      unrecognized_tags.insert(child.children.front().text);
    }
  }
  for (const std::string& tag : unrecognized_tags) {
    add_record(report,
               EntityDomain::Geometry,
               core::EntityId{},
               tag,
               "presence",
               FidelityTier::Unsupported,
               "'(" + tag + " ...)' is not recognized by this importer");
  }
}

void populate_import_fidelity_report(const SExpr& kicad_pcb,
                                     const pcbir::BoardSnapshot& board,
                                     FidelityReport& report) {
  populate_stackup_fidelity(report, board.stackup);
  populate_geometry_fidelity(report, board.geometry, board.extensions);
  populate_unsupported_sections_fidelity(report, kicad_pcb);
}

} // namespace

pcbir::BoardSnapshot import_kicad_pcb(const std::filesystem::path& path, FidelityReport* report) {
  const SExpr root = parse_sexpr(read_file(path));
  validate_kicad_pcb_root(root);

  const stackup::StackupSnapshot stackup = import_stackup(root);
  const connectivity::ConnectivitySnapshot nets = import_nets(root);

  const geometry::GeometrySnapshot after_outline = import_board_outline(root, stackup);
  const FootprintImportResult after_footprints =
      import_footprints(root, stackup, nets, after_outline);
  const geometry::GeometrySnapshot after_tracks =
      import_tracks(root, stackup, after_footprints.connectivity, after_footprints.geometry);
  ViaImportResult after_vias =
      import_vias(root, stackup, after_footprints.connectivity, after_tracks);
  geometry::GeometrySnapshot final_geometry =
      import_zones(root, stackup, after_vias.connectivity, after_vias.geometry);

  pcbir::BoardSnapshot board;
  board.geometry = std::move(final_geometry);
  board.connectivity = std::move(after_vias.connectivity);
  board.stackup = stackup;
  board.extensions = after_footprints.extensions;

  if (report != nullptr) {
    populate_import_fidelity_report(root, board, *report);
  }
  return board;
}

stackup::StackupSnapshot import_stackup(const SExpr& kicad_pcb) {
  const SExpr* layers_section = find_child(kicad_pcb, "layers");
  if (layers_section == nullptr) {
    throw ImportError("missing (layers ...) section");
  }

  stackup::StackupWorkspace workspace;
  std::vector<KicadLayerRow> copper_rows;

  for (const SExpr& row : layers_section->children) {
    if (!row.is_list()) {
      continue; // Only nested lists are layer entries.
    }
    const KicadLayerRow layer_row = parse_layer_row(row);

    if (is_copper(layer_row.name)) {
      copper_rows.push_back(layer_row);
    } else if (layer_row.name == "Edge.Cuts") {
      workspace.insert(stackup::Layer{.name = layer_row.name,
                                      .kind = stackup::LayerKind::EdgeCuts,
                                      .thickness_nm = 0,
                                      .roughness_nm = 0,
                                      .material = core::EntityId{}});
    } else if (is_silkscreen(layer_row.name)) {
      workspace.insert(stackup::Layer{.name = layer_row.name,
                                      .kind = stackup::LayerKind::Silkscreen,
                                      .thickness_nm = 0,
                                      .roughness_nm = 0,
                                      .material = core::EntityId{}});
    }
    // Every other KiCad layer (F.Mask, F.Paste, F.Fab, F.CrtYd,
    // Dwgs.User, ...) has no stackup::LayerKind yet (import.hpp) and is
    // silently skipped.
  }

  if (!copper_rows.empty()) {
    // copper_rows is already in physical top-to-bottom stackup order: it's
    // built by a single pass over (layers ...)'s own children in file
    // order, and that file order -- not the numeric id -- is what encodes
    // physical order. Verified against real pcbnew output on 2/4/6-copper-
    // layer boards (docs/rfcs/0003-kicad-importer-exporter.md): a board's
    // id for B.Cu is always the fixed value 2 regardless of copper layer
    // count (KiCad's internal PCB_LAYER_ID enum reserves 2 for B.Cu
    // specifically), while inner layers get ids 4, 6, 8, ... in physical
    // order -- so sorting copper_rows by id (this function's original,
    // incorrect approach, corrected here) places B.Cu second-from-top on
    // any board with 2+ inner layers instead of last. pcbnew itself
    // doesn't trust a loaded file's id for a named layer either (confirmed
    // by editing a probe file's B.Cu id and reloading: pcbnew re-derives
    // the canonical id from the name and loads it unchanged) -- id is a
    // per-name tag, not a position encoding.

    std::vector<core::EntityId> copper_layer_ids;
    copper_layer_ids.reserve(copper_rows.size());
    for (const KicadLayerRow& row : copper_rows) {
      const auto handle =
          workspace.insert(stackup::Layer{.name = row.name,
                                          .kind = stackup::LayerKind::Copper,
                                          .thickness_nm = DEFAULT_COPPER_THICKNESS_NM,
                                          .roughness_nm = 0,
                                          .material = core::EntityId{}});
      copper_layer_ids.push_back(workspace.table<stackup::Layer>().id_of(handle));
    }

    workspace.insert(stackup::LayerStack{.name = "", .layers = std::move(copper_layer_ids)});
  }

  return workspace.commit();
}

} // namespace pcbir::kicad
