// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import_footprint.hpp"

#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/entity_domain.hpp"
#include "pcbir/extension.hpp"
#include "pcbir/geometry/checked_arith.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/rotate.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coordinate_util.hpp"
#include "pad_shape.hpp"
#include "pad_shape_extension.hpp"
#include "sexpr_util.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Point;
using geometry::Polygon;

[[nodiscard]] std::string find_property_value(const SExpr& footprint, std::string_view key) {
  for (const SExpr* property : find_all_children(footprint, "property")) {
    if (property->children.size() >= 3 && property->children.at(1).is_string() &&
        property->children.at(1).text == key) {
      return property->children.at(2).text;
    }
  }
  return "";
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
  // "custom" pads (primitive-list-defined) don't go through this
  // parametric-shape path at all -- see parse_custom_pad_outline below,
  // called directly by import_pad for shape == "custom" before this
  // function would ever be reached with that text.
  throw ImportError("unsupported KiCad pad shape: '" + text + "'");
}

// A `custom` pad's local (pad-anchor-relative, unrotated-by-the-pad's-own-
// angle) outline, parsed from its `(primitives (gr_poly (pts ...)) ...)`
// -- the counterpart to build_pad_outline (pad_shape.hpp) for the one
// shape that isn't a fixed parametric primitive. Real KiCad custom pads
// can combine several primitives of several kinds (gr_poly/gr_line/
// gr_arc/gr_rect/gr_circle); this pass supports only the single-gr_poly
// case (docs/rfcs/0003-kicad-importer-exporter.md's exporter always emits
// exactly that -- see export_footprint.cpp), so a custom pad built any
// other way is Unsupported and throws, not silently approximated.
[[nodiscard]] Polygon parse_custom_pad_outline(const SExpr& pad) {
  const SExpr* primitives_node = find_child(pad, "primitives");
  if (primitives_node == nullptr) {
    throw ImportError("malformed custom (pad ...): missing (primitives ...)");
  }
  const std::vector<const SExpr*> polygons = find_all_children(*primitives_node, "gr_poly");
  if (polygons.size() != 1) {
    throw ImportError("unsupported custom (pad ...): expected exactly one (gr_poly ...) primitive");
  }
  const SExpr* pts_node = find_child(*polygons.front(), "pts");
  if (pts_node == nullptr || pts_node->children.size() < 4) {
    throw ImportError("malformed (gr_poly ...): (pts ...) needs at least 3 points");
  }

  std::vector<Point> points;
  points.reserve(pts_node->children.size() - 1);
  for (std::size_t i = 1; i < pts_node->children.size(); ++i) {
    const SExpr& entry = pts_node->children.at(i);
    if (!entry.is_list() || entry.children.empty() || !entry.children.front().is_symbol() ||
        entry.children.front().text != "xy") {
      throw ImportError("unsupported (gr_poly ...) (pts ...) entry: expected only (xy X Y)");
    }
    points.push_back(parse_coordinate_pair(entry));
  }

  std::vector<geometry::Span> spans;
  spans.reserve(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    spans.emplace_back(
        geometry::Segment{.start = points.at(i), .end = points.at((i + 1) % points.size())});
  }
  return Polygon{.outline = canonicalize_winding(geometry::Contour{.spans = std::move(spans)}),
                 .holes = {}};
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
// Footprint::pads. A non-custom-shape Pad also appends a PCBIR_KICAD/
// pad_shape Extension to `extensions` (pad_shape_extension.hpp), keyed by
// the new Pad's own id.
[[nodiscard]] core::EntityId import_pad(const SExpr& pad,
                                        const stackup::StackupSnapshot& stackup,
                                        const std::map<std::string, core::EntityId>& net_index,
                                        const Point& footprint_position,
                                        int64_t footprint_rotation_e6,
                                        geometry::GeometryWorkspace& geometry_workspace,
                                        connectivity::ConnectivityWorkspace& connectivity_workspace,
                                        std::vector<Extension>& extensions) {
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
    const std::string& shape_text = pad.children.at(3).text;
    std::optional<PadShapeParams> shape_params;
    Polygon local_outline;
    if (shape_text == "custom") {
      local_outline = parse_custom_pad_outline(pad);
    } else {
      shape_params = parse_pad_shape_params(pad, parse_pad_shape_kind(shape_text));
      local_outline = build_pad_outline(*shape_params);
    }
    const Polygon absolute_outline =
        translate(geometry::rotate(local_outline, Point{.x = 0, .y = 0}, placement.rotation_e6),
                  placement.center);
    const auto handle =
        geometry_workspace.insert(geometry::Pad{.position = placement.center,
                                                .outline = absolute_outline,
                                                .layer = resolve_pad_copper_layer(stackup, pad),
                                                .pad_number = pad_number});
    entity_id = geometry_workspace.table<geometry::Pad>().id_of(handle);
    if (shape_params.has_value()) {
      extensions.push_back(
          Extension{.domain = EntityDomain::Geometry,
                    .entity_id = entity_id,
                    .ext_namespace = PAD_SHAPE_EXTENSION_NAMESPACE,
                    .name = PAD_SHAPE_EXTENSION_NAME,
                    .version = PAD_SHAPE_EXTENSION_VERSION,
                    .payload = encode_pad_shape_extension(*shape_params, placement.rotation_e6)});
    }
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
                                        const connectivity::ConnectivitySnapshot& nets,
                                        const geometry::GeometrySnapshot& geometry_base) {
  const std::map<std::string, core::EntityId> net_index = build_net_index(nets);

  geometry::GeometryWorkspace geometry_workspace(geometry_base);
  connectivity::ConnectivityWorkspace connectivity_workspace(nets);
  std::vector<Extension> extensions;

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
                                   connectivity_workspace,
                                   extensions));
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
                               .connectivity = connectivity_workspace.commit(),
                               .extensions = std::move(extensions)};
}

} // namespace pcbir::kicad
