// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/export.hpp"

#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/format_version.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/path.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/export_footprint.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "coordinate_util.hpp"
#include "sexpr_build.hpp"
#include "uuid.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Arc;
using geometry::Contour;
using geometry::Segment;
using geometry::Span;

// KiCad's own fixed copper-layer-id scheme has room for exactly this many
// entries (F.Cu, B.Cu, In1.Cu..In30.Cu) -- see copper_layer_id below.
constexpr std::size_t MAX_COPPER_LAYERS = 32;

// Real pcbnew-authored board outline strokes default to this width
// (docs/rfcs/0003-kicad-importer-exporter.md's probe boards, e.g.
// tests/corpus/kicad/rounded-rect-outline/board.kicad_pcb); geometry::
// BoardOutline carries no per-span width field to preserve instead (unlike
// geometry::Track/Path), so every exported edge uses this fixed value --
// an Approximated-tier gap, the same "field KiCad has, schema doesn't"
// shape as import_stackup's DEFAULT_COPPER_THICKNESS_NM.
constexpr const char* DEFAULT_EDGE_STROKE_WIDTH_MM = "0.1";

// KiCad's fixed numeric layer id for the copper layer at `position`
// (0-based, 0 = front) of a `count`-layer physical stack: F.Cu is always
// 0, B.Cu is always 2, and inner layers get 4, 6, 8, ... in physical order
// (export.hpp; verified against real pcbnew output on 2/4/6-copper-layer
// boards).
[[nodiscard]] int64_t copper_layer_id(std::size_t position, std::size_t count) {
  if (position == 0) {
    return 0;
  }
  if (position == count - 1) {
    return 2;
  }
  return (static_cast<int64_t>(position) * 2) + 2;
}

[[nodiscard]] SExpr layer_row(int64_t id,
                              const std::string& name,
                              const char* type,
                              const std::string& friendly_name = "") {
  std::vector<SExpr> children{sym(std::to_string(id)), str(name), sym(type)};
  if (!friendly_name.empty()) {
    children.push_back(str(friendly_name));
  }
  return list(std::move(children));
}

} // namespace

SExpr export_layers_section(const stackup::StackupSnapshot& stackup) {
  std::vector<SExpr> rows;

  const stackup::LayerStack* stack = nullptr;
  stackup.table<stackup::LayerStack>().for_each(
      [&](core::EntityId, const stackup::LayerStack& candidate) { stack = &candidate; });

  if (stack != nullptr) {
    if (stack->layers.size() > MAX_COPPER_LAYERS) {
      throw ExportError("LayerStack has more copper layers than KiCad's format supports (32)");
    }
    const auto& layers = stackup.table<stackup::Layer>();
    for (std::size_t i = 0; i < stack->layers.size(); ++i) {
      const stackup::Layer* layer = layers.try_get(layers.find(stack->layers.at(i)));
      if (layer == nullptr) {
        throw ExportError("LayerStack references a Layer id not present in this stackup");
      }
      rows.push_back(layer_row(copper_layer_id(i, stack->layers.size()), layer->name, "signal"));
    }
  }

  stackup.table<stackup::Layer>().for_each([&](core::EntityId, const stackup::Layer& layer) {
    if (layer.kind == stackup::LayerKind::EdgeCuts) {
      rows.push_back(layer_row(25, layer.name, "user"));
    } else if (layer.kind == stackup::LayerKind::Silkscreen) {
      const bool front = layer.name == "F.SilkS";
      rows.push_back(
          layer_row(front ? 5 : 7, layer.name, "user", front ? "F.Silkscreen" : "B.Silkscreen"));
    }
  });

  return tagged("layers", std::move(rows));
}

namespace {

[[nodiscard]] core::EntityId find_edge_cuts_layer(const stackup::StackupSnapshot& stackup) {
  core::EntityId id;
  stackup.table<stackup::Layer>().for_each(
      [&](core::EntityId candidate_id, const stackup::Layer& layer) {
        if (layer.kind == stackup::LayerKind::EdgeCuts) {
          id = candidate_id;
        }
      });
  return id;
}

[[nodiscard]] SExpr stroke_node() {
  return tagged(
      "stroke",
      {tagged("width", {sym(DEFAULT_EDGE_STROKE_WIDTH_MM)}), tagged("type", {sym("default")})});
}

[[nodiscard]] SExpr
export_edge_span(const Span& span, core::EntityId outline_id, std::size_t index_in_outline) {
  const std::string uuid_name =
      "pcbir:geometry:board_outline_span:" + std::to_string(outline_id.value()) + ":" +
      std::to_string(index_in_outline);
  SExpr uuid_field = tagged("uuid", {str(uuid_v5(UUID_NAMESPACE, uuid_name))});
  SExpr layer_field = tagged("layer", {str("Edge.Cuts")});

  if (const auto* segment = std::get_if<Segment>(&span)) {
    return tagged("gr_line",
                  {coordinate_pair("start", segment->start),
                   coordinate_pair("end", segment->end),
                   stroke_node(),
                   std::move(layer_field),
                   std::move(uuid_field)});
  }
  const auto& arc = std::get<Arc>(span);
  return tagged("gr_arc",
                {coordinate_pair("start", arc.start),
                 coordinate_pair("mid", arc_three_point_mid(arc)),
                 coordinate_pair("end", arc.end),
                 stroke_node(),
                 std::move(layer_field),
                 std::move(uuid_field)});
}

} // namespace

std::vector<SExpr> export_board_outline(const geometry::GeometrySnapshot& geometry,
                                        const stackup::StackupSnapshot& stackup) {
  const core::EntityId edge_cuts_id = find_edge_cuts_layer(stackup);
  if (edge_cuts_id.is_null()) {
    throw ExportError("stackup has no Edge.Cuts layer");
  }

  std::vector<SExpr> nodes;
  geometry.table<geometry::BoardOutline>().for_each(
      [&](core::EntityId id, const geometry::BoardOutline& outline) {
        if (outline.layer != edge_cuts_id) {
          throw ExportError("BoardOutline references a layer that isn't Edge.Cuts");
        }
        const std::vector<Span>& spans = outline.outline.outline.spans;
        for (std::size_t i = 0; i < spans.size(); ++i) {
          nodes.push_back(export_edge_span(spans.at(i), id, i));
        }
      });
  return nodes;
}

namespace {

[[nodiscard]] SExpr export_track_span(const geometry::WidthSpan& width_span,
                                      const stackup::Layer& layer,
                                      std::optional<SExpr> net,
                                      core::EntityId track_id,
                                      std::size_t index_in_path) {
  const std::string uuid_name = "pcbir:geometry:track_span:" + std::to_string(track_id.value()) +
                                ":" + std::to_string(index_in_path);

  std::vector<SExpr> children;
  const Span& span = width_span.geometry;
  if (const auto* segment = std::get_if<Segment>(&span)) {
    children.push_back(coordinate_pair("start", segment->start));
    children.push_back(coordinate_pair("end", segment->end));
  } else {
    const auto& arc = std::get<Arc>(span);
    children.push_back(coordinate_pair("start", arc.start));
    children.push_back(coordinate_pair("mid", arc_three_point_mid(arc)));
    children.push_back(coordinate_pair("end", arc.end));
  }
  children.push_back(tagged("width", {sym(format_nm_to_mm(width_span.width_nm))}));
  children.push_back(tagged("layer", {str(layer.name)}));
  push_if_present(children, std::move(net));
  children.push_back(tagged("uuid", {str(uuid_v5(UUID_NAMESPACE, uuid_name))}));

  return tagged(std::holds_alternative<Arc>(span) ? "arc" : "segment", std::move(children));
}

} // namespace

std::vector<SExpr> export_tracks(const geometry::GeometrySnapshot& geometry,
                                 const stackup::StackupSnapshot& stackup,
                                 const connectivity::ConnectivitySnapshot& nets) {
  std::vector<SExpr> nodes;
  geometry.table<geometry::Track>().for_each([&](core::EntityId id, const geometry::Track& track) {
    const stackup::Layer& layer = require_layer(stackup, track.layer);
    for (std::size_t i = 0; i < track.path.spans.size(); ++i) {
      nodes.push_back(
          export_track_span(track.path.spans.at(i), layer, net_field(nets, track.net), id, i));
    }
  });
  return nodes;
}

namespace {

[[nodiscard]] std::unordered_set<core::EntityId::ValueType>
collect_footprint_owned_ids(const geometry::GeometrySnapshot& geometry) {
  std::unordered_set<core::EntityId::ValueType> owned;
  geometry.table<geometry::Footprint>().for_each(
      [&](core::EntityId, const geometry::Footprint& footprint) {
        for (const core::EntityId pad_id : footprint.pads) {
          owned.insert(pad_id.value());
        }
      });
  return owned;
}

[[nodiscard]] SExpr export_via_entry(const geometry::Via& via,
                                     const stackup::Layer& start_layer,
                                     const stackup::Layer& end_layer,
                                     std::optional<SExpr> net,
                                     core::EntityId via_id) {
  const std::string uuid_name = "pcbir:geometry:via:" + std::to_string(via_id.value());

  std::vector<SExpr> children;
  children.push_back(coordinate_pair("at", via.position));
  children.push_back(tagged("size", {sym(format_nm_to_mm(via.pad_diameter_nm))}));
  children.push_back(tagged("drill", {sym(format_nm_to_mm(via.drill_diameter_nm))}));
  children.push_back(tagged("layers", {str(start_layer.name), str(end_layer.name)}));
  push_if_present(children, std::move(net));
  children.push_back(tagged("uuid", {str(uuid_v5(UUID_NAMESPACE, uuid_name))}));

  return tagged("via", std::move(children));
}

} // namespace

std::vector<SExpr> export_vias(const geometry::GeometrySnapshot& geometry,
                               const stackup::StackupSnapshot& stackup,
                               const connectivity::ConnectivitySnapshot& nets) {
  const std::unordered_set<core::EntityId::ValueType> owned = collect_footprint_owned_ids(geometry);
  const std::map<core::EntityId::ValueType, core::EntityId> pin_nets = build_pin_net_index(nets);

  std::vector<SExpr> nodes;
  geometry.table<geometry::Via>().for_each([&](core::EntityId id, const geometry::Via& via) {
    if (owned.contains(id.value())) {
      return; // A footprint's thru-hole pad, not a free via -- export_footprints' job.
    }
    const stackup::Layer& start_layer = require_layer(stackup, via.start_layer);
    const stackup::Layer& end_layer = require_layer(stackup, via.end_layer);
    const auto pin_it = pin_nets.find(id.value());
    const core::EntityId net_id = pin_it != pin_nets.end() ? pin_it->second : core::EntityId{};
    nodes.push_back(export_via_entry(via, start_layer, end_layer, net_field(nets, net_id), id));
  });
  return nodes;
}

namespace {

// A zone's `(polygon (pts (xy X Y) ...))` node for `contour` -- the
// exporter's inverse of import_zone.cpp's parse_zone_polygon, recovering
// the original point list from each span's start point (adjacent spans in
// a closed Contour share endpoints, so the start points alone already
// trace the whole loop). Throws ExportError on an Arc span: KiCad's zone
// `(pts ...)` supports only straight-edge points, the same restriction
// import_zones documents as Unsupported for an arc-cornered zone.
[[nodiscard]] SExpr export_zone_polygon(const Contour& contour) {
  std::vector<SExpr> points;
  points.reserve(contour.spans.size());
  for (const Span& span : contour.spans) {
    if (std::holds_alternative<Arc>(span)) {
      throw ExportError("CopperPour outline/hole has an Arc span -- KiCad's zone (pts ...) "
                        "supports only straight edges");
    }
    points.push_back(coordinate_pair("xy", geometry::span_start(span)));
  }
  return tagged("polygon", {tagged("pts", std::move(points))});
}

[[nodiscard]] SExpr export_zone_entry(const geometry::CopperPour& pour,
                                      const stackup::Layer& layer,
                                      std::optional<SExpr> net,
                                      core::EntityId pour_id) {
  const std::string uuid_name = "pcbir:geometry:copper_pour:" + std::to_string(pour_id.value());

  std::vector<SExpr> children;
  push_if_present(children, std::move(net));
  children.push_back(tagged("layer", {str(layer.name)}));
  children.push_back(tagged("uuid", {str(uuid_v5(UUID_NAMESPACE, uuid_name))}));
  children.push_back(export_zone_polygon(pour.outline.outline));
  for (const Contour& hole : pour.outline.holes) {
    children.push_back(export_zone_polygon(hole));
  }

  return tagged("zone", std::move(children));
}

} // namespace

std::vector<SExpr> export_zones(const geometry::GeometrySnapshot& geometry,
                                const stackup::StackupSnapshot& stackup,
                                const connectivity::ConnectivitySnapshot& nets) {
  std::vector<SExpr> nodes;
  geometry.table<geometry::CopperPour>().for_each(
      [&](core::EntityId id, const geometry::CopperPour& pour) {
        const stackup::Layer& layer = require_layer(stackup, pour.layer);
        nodes.push_back(export_zone_entry(pour, layer, net_field(nets, pour.net), id));
      });
  return nodes;
}

namespace {

// This importer/exporter's pinned KiCad board-format version (import.hpp's
// own MIN_SUPPORTED_KICAD_VERSION) -- every file this exporter writes
// declares exactly the version it was verified against, never an older or
// newer one.
constexpr int64_t EXPORTED_KICAD_VERSION = 20260206;

// A board's overall thickness has no PCB-IR representation (stackup only
// tracks each individual Layer's own thickness_nm, never a top-level sum) --
// the same "field KiCad has, schema doesn't" shape as
// DEFAULT_EDGE_STROKE_WIDTH_MM above, fixed to the value real pcbnew output
// uses for a simple 2-layer board (tests/corpus/kicad/rounded-rect-outline/
// board.kicad_pcb).
constexpr const char* DEFAULT_BOARD_THICKNESS_MM = "1.6";

[[nodiscard]] SExpr general_section() {
  return tagged("general", {tagged("thickness", {sym(DEFAULT_BOARD_THICKNESS_MM)})});
}

// A minimal but valid `(setup ...)` -- real pcbnew output carries a much
// larger block (plot params, tenting, ...), none of which PCB-IR's schema
// has a representation for; pcbnew fills in its own defaults for whatever
// a loaded file's (setup ...) omits, so this doesn't lose information PCB-
// IR ever had, only KiCad-tool-preference data PCB-IR never captured in
// the first place (Unsupported, not Approximated -- there is no source
// value to approximate towards).
[[nodiscard]] SExpr minimal_setup_section() {
  return tagged("setup", {tagged("pad_to_mask_clearance", {sym("0")})});
}

void extend(std::vector<SExpr>& dest, std::vector<SExpr> src) {
  dest.reserve(dest.size() + src.size());
  for (SExpr& node : src) {
    dest.push_back(std::move(node));
  }
}

} // namespace

void export_kicad_pcb(const pcbir::BoardSnapshot& board, const std::filesystem::path& path) {
  std::vector<SExpr> children;
  children.push_back(tagged("version", {sym(std::to_string(EXPORTED_KICAD_VERSION))}));
  children.push_back(tagged("generator", {str("pcbir")}));
  children.push_back(tagged("generator_version",
                            {str(std::to_string(pcbir::CURRENT_FORMAT_VERSION.major) + "." +
                                 std::to_string(pcbir::CURRENT_FORMAT_VERSION.minor))}));
  children.push_back(general_section());
  children.push_back(tagged("paper", {str("A4")}));
  children.push_back(export_layers_section(board.stackup));
  children.push_back(minimal_setup_section());

  extend(children, export_board_outline(board.geometry, board.stackup));
  extend(children,
         export_footprints(board.geometry, board.stackup, board.connectivity, board.extensions));
  extend(children, export_tracks(board.geometry, board.stackup, board.connectivity));
  extend(children, export_vias(board.geometry, board.stackup, board.connectivity));
  extend(children, export_zones(board.geometry, board.stackup, board.connectivity));

  children.push_back(tagged("embedded_fonts", {sym("no")}));

  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw ExportError("failed to open '" + path.string() + "' for writing");
  }
  file << write_sexpr(tagged("kicad_pcb", std::move(children)));
}

} // namespace pcbir::kicad
