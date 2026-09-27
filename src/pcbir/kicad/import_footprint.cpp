// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import_footprint.hpp"

#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/checked_arith.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/rotate.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coordinate_util.hpp"
#include "pad_shape.hpp"
#include "sexpr_util.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Point;
using geometry::Polygon;

[[nodiscard]] std::vector<std::string> parse_layer_names(const SExpr& layers_node) {
  std::vector<std::string> names;
  names.reserve(layers_node.children.size());
  for (std::size_t i = 1; i < layers_node.children.size(); ++i) {
    names.push_back(layers_node.children.at(i).text);
  }
  return names;
}

[[nodiscard]] std::string find_property_value(const SExpr& footprint, std::string_view key) {
  for (const SExpr* property : find_all_children(footprint, "property")) {
    if (property->children.size() >= 3 && property->children.at(1).is_string() &&
        property->children.at(1).text == key) {
      return property->children.at(2).text;
    }
  }
  return "";
}

[[nodiscard]] const stackup::LayerStack&
require_layer_stack(const stackup::StackupSnapshot& stackup) {
  const stackup::LayerStack* stack = nullptr;
  stackup.table<stackup::LayerStack>().for_each(
      [&](core::EntityId, const stackup::LayerStack& candidate) { stack = &candidate; });
  if (stack == nullptr || stack->layers.empty()) {
    throw ImportError("via/thru-hole pad requires a non-empty stackup LayerStack");
  }
  return *stack;
}

[[nodiscard]] std::size_t layer_stack_position(const stackup::LayerStack& stack,
                                               core::EntityId id) {
  for (std::size_t i = 0; i < stack.layers.size(); ++i) {
    if (stack.layers.at(i) == id) {
      return i;
    }
  }
  throw ImportError("via/thru-hole pad references a copper layer outside the LayerStack");
}

// Resolves a via/thru-hole pad's `(layers ...)` entry to its top-to-bottom
// ordered (start, end) layer span: either the `"*.Cu"` wildcard (every
// copper layer, per import_stackup's own numeric-id-order-is-physical-
// order finding) or exactly 2 explicit copper layer names.
[[nodiscard]] std::pair<core::EntityId, core::EntityId>
resolve_via_layer_span(const stackup::StackupSnapshot& stackup,
                       const std::vector<std::string>& layer_names) {
  const stackup::LayerStack& stack = require_layer_stack(stackup);

  if (layer_names.size() == 1 && layer_names.front() == "*.Cu") {
    return {stack.layers.front(), stack.layers.back()};
  }
  if (layer_names.size() < 2) {
    throw ImportError("malformed via/thru-hole pad (layers ...): expected \"*.Cu\" or 2 names");
  }
  const core::EntityId first = find_layer_id_by_name(stackup, layer_names.at(0));
  const core::EntityId second = find_layer_id_by_name(stackup, layer_names.at(1));
  if (first.is_null() || second.is_null()) {
    throw ImportError("via/thru-hole pad references an unrecognized copper layer");
  }
  return (layer_stack_position(stack, first) <= layer_stack_position(stack, second))
             ? std::pair{first, second}
             : std::pair{second, first};
}

[[nodiscard]] KicadPadShape parse_pad_shape_kind(const std::string& text) {
  if (text == "rect") {
    return KicadPadShape::Rect;
  }
  if (text == "circle") {
    return KicadPadShape::Circle;
  }
  if (text == "oval") {
    return KicadPadShape::Oval;
  }
  if (text == "roundrect") {
    return KicadPadShape::RoundRect;
  }
  if (text == "trapezoid") {
    return KicadPadShape::Trapezoid;
  }
  // "custom" (primitive-list-defined) pads are explicitly out of scope
  // for this pass (docs/rfcs/0003-kicad-importer-exporter.md).
  throw ImportError("unsupported KiCad pad shape: '" + text + "'");
}

[[nodiscard]] PadShapeParams parse_pad_shape_params(const SExpr& pad, KicadPadShape shape) {
  const SExpr* size_node = find_child(pad, "size");
  if (size_node == nullptr || size_node->children.size() < 3) {
    throw ImportError("malformed (pad ...): missing (size W H)");
  }
  PadShapeParams params{.shape = shape,
                        .width_nm = parse_mm_to_nm(size_node->children.at(1).text),
                        .height_nm = parse_mm_to_nm(size_node->children.at(2).text)};

  if (shape == KicadPadShape::RoundRect) {
    if (const SExpr* ratio_node = find_child(pad, "roundrect_rratio"); ratio_node != nullptr) {
      const int64_t ratio_e6 = parse_ratio_to_e6(ratio_node->children.at(1).text);
      const int64_t min_dim_nm = std::min(params.width_nm, params.height_nm);
      int64_t scaled = 0;
      if (!geometry::checked_mul(ratio_e6, min_dim_nm, scaled)) {
        throw ImportError("roundrect_rratio computation overflows");
      }
      params.roundrect_radius_nm = scaled / 1'000'000;
    }
  } else if (shape == KicadPadShape::Trapezoid) {
    if (const SExpr* delta_node = find_child(pad, "rect_delta"); delta_node != nullptr) {
      if (delta_node->children.size() < 3) {
        throw ImportError("malformed (pad ...): (rect_delta ...) needs 2 values");
      }
      params.trapezoid_delta_x_nm = parse_mm_to_nm(delta_node->children.at(1).text);
      params.trapezoid_delta_y_nm = parse_mm_to_nm(delta_node->children.at(2).text);
    }
  }
  return params;
}

// A pad/via's absolute center and (for a Pad, not a Via) absolute
// rotation, both in PCB-IR's counterclockwise angle_e6 convention.
struct AbsolutePlacement {
  Point center;
  int64_t rotation_e6 = 0;
};

[[nodiscard]] AbsolutePlacement resolve_absolute_placement(const SExpr& pad,
                                                           const Point& footprint_position,
                                                           int64_t footprint_rotation_e6) {
  const SExpr* at_node = find_child(pad, "at");
  if (at_node == nullptr) {
    throw ImportError("malformed (pad ...): missing (at ...)");
  }
  const Point local_offset = parse_coordinate_pair(*at_node);
  const Point center = footprint_position +
                       geometry::rotate(local_offset, Point{.x = 0, .y = 0}, footprint_rotation_e6);
  return AbsolutePlacement{.center = center, .rotation_e6 = parse_kicad_rotation_e6(*at_node)};
}

[[nodiscard]] core::EntityId resolve_pad_copper_layer(const stackup::StackupSnapshot& stackup,
                                                      const SExpr& pad) {
  const SExpr* layers_node = find_child(pad, "layers");
  if (layers_node == nullptr) {
    throw ImportError("malformed (pad ...): missing (layers ...)");
  }
  for (const std::string& name : parse_layer_names(*layers_node)) {
    if (name.ends_with(".Cu") && name != "*.Cu") {
      const core::EntityId id = find_layer_id_by_name(stackup, name);
      if (!id.is_null()) {
        return id;
      }
    }
  }
  throw ImportError("(pad ...) has no recognized copper layer in its (layers ...)");
}

// Imports one `(pad ...)` (smd/connect -> Pad, thru_hole/np_thru_hole ->
// Via) and inserts the result plus its Pin into the given workspaces,
// returning the new entity's id so the caller can add it to its owning
// Footprint::pads.
[[nodiscard]] core::EntityId
import_pad(const SExpr& pad,
           const stackup::StackupSnapshot& stackup,
           const std::map<std::string, core::EntityId>& net_index,
           const Point& footprint_position,
           int64_t footprint_rotation_e6,
           geometry::GeometryWorkspace& geometry_workspace,
           connectivity::ConnectivityWorkspace& connectivity_workspace) {
  if (pad.children.size() < 4 || !pad.children.at(1).is_string() ||
      !pad.children.at(2).is_symbol() || !pad.children.at(3).is_symbol()) {
    throw ImportError("malformed (pad ...): expected (pad \"NUMBER\" TYPE SHAPE ...)");
  }
  const std::string& pad_number = pad.children.at(1).text;
  const std::string& pad_type = pad.children.at(2).text;
  const AbsolutePlacement placement =
      resolve_absolute_placement(pad, footprint_position, footprint_rotation_e6);
  const core::EntityId net_id = resolve_optional_net(pad, net_index);

  core::EntityId entity_id;
  if (pad_type == "smd" || pad_type == "connect") {
    const KicadPadShape shape = parse_pad_shape_kind(pad.children.at(3).text);
    const PadShapeParams shape_params = parse_pad_shape_params(pad, shape);
    const Polygon local_outline = build_pad_outline(shape_params);
    const Polygon absolute_outline =
        translate(geometry::rotate(local_outline, Point{.x = 0, .y = 0}, placement.rotation_e6),
                  placement.center);
    const auto handle =
        geometry_workspace.insert(geometry::Pad{.position = placement.center,
                                                .outline = absolute_outline,
                                                .layer = resolve_pad_copper_layer(stackup, pad),
                                                .pad_number = pad_number});
    entity_id = geometry_workspace.table<geometry::Pad>().id_of(handle);
  } else if (pad_type == "thru_hole" || pad_type == "np_thru_hole") {
    const SExpr* size_node = find_child(pad, "size");
    const SExpr* drill_node = find_child(pad, "drill");
    const SExpr* layers_node = find_child(pad, "layers");
    if (size_node == nullptr || size_node->children.size() < 2 || drill_node == nullptr ||
        drill_node->children.size() < 2 || layers_node == nullptr) {
      throw ImportError(
          "malformed thru-hole (pad ...): missing (size ...)/(drill ...)/(layers ...)");
    }
    const int64_t pad_diameter_nm = parse_mm_to_nm(size_node->children.at(1).text);
    // KiCad's per-pad (drill ...) gives only the drill diameter; the
    // finished (plated) hole diameter is a board-wide design-rule value
    // this format doesn't repeat per pad, so it's approximated as equal
    // to the drill diameter (documented Approximated-tier gap, not a
    // silent one).
    const int64_t drill_diameter_nm = parse_mm_to_nm(drill_node->children.at(1).text);
    const auto [start_layer, end_layer] =
        resolve_via_layer_span(stackup, parse_layer_names(*layers_node));
    const auto handle =
        geometry_workspace.insert(geometry::Via{.position = placement.center,
                                                .drill_diameter_nm = drill_diameter_nm,
                                                .finished_hole_diameter_nm = drill_diameter_nm,
                                                .pad_diameter_nm = pad_diameter_nm,
                                                .start_layer = start_layer,
                                                .end_layer = end_layer,
                                                .pad_number = pad_number});
    entity_id = geometry_workspace.table<geometry::Via>().id_of(handle);
  } else {
    throw ImportError("unsupported KiCad pad type: '" + pad_type + "'");
  }

  connectivity_workspace.insert(connectivity::Pin{.pad = entity_id, .net = net_id});
  return entity_id;
}

} // namespace

FootprintImportResult import_footprints(const SExpr& kicad_pcb,
                                        const stackup::StackupSnapshot& stackup,
                                        const connectivity::ConnectivitySnapshot& nets) {
  const std::map<std::string, core::EntityId> net_index = build_net_index(nets);

  geometry::GeometryWorkspace geometry_workspace;
  connectivity::ConnectivityWorkspace connectivity_workspace(nets);

  for (const SExpr* footprint : find_all_children(kicad_pcb, "footprint")) {
    const SExpr* layer_node = find_child(*footprint, "layer");
    const SExpr* at_node = find_child(*footprint, "at");
    if (layer_node == nullptr || layer_node->children.size() < 2 || at_node == nullptr) {
      throw ImportError("malformed (footprint ...): missing (layer ...)/(at ...)");
    }
    const Point footprint_position = parse_coordinate_pair(*at_node);
    const int64_t footprint_rotation_e6 = parse_kicad_rotation_e6(*at_node);
    const geometry::FootprintSide side = (layer_node->children.at(1).text == "B.Cu")
                                             ? geometry::FootprintSide::Bottom
                                             : geometry::FootprintSide::Top;

    std::vector<core::EntityId> pad_ids;
    for (const SExpr* pad : find_all_children(*footprint, "pad")) {
      pad_ids.push_back(import_pad(*pad,
                                   stackup,
                                   net_index,
                                   footprint_position,
                                   footprint_rotation_e6,
                                   geometry_workspace,
                                   connectivity_workspace));
    }

    geometry_workspace.insert(
        geometry::Footprint{.reference_designator = find_property_value(*footprint, "Reference"),
                            .value = find_property_value(*footprint, "Value"),
                            .position = footprint_position,
                            .rotation_e6 = footprint_rotation_e6,
                            .side = side,
                            .pads = std::move(pad_ids)});
  }

  return FootprintImportResult{.geometry = geometry_workspace.commit(),
                               .connectivity = connectivity_workspace.commit()};
}

} // namespace pcbir::kicad
