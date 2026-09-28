// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/export.hpp"

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "coordinate_util.hpp"
#include "sexpr_build.hpp"
#include "uuid.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Arc;
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

} // namespace pcbir::kicad
