// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import.hpp"

#include "pcbir/core/entity_id.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
