// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/export_footprint.hpp"

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/export.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
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
using geometry::Point;
using geometry::Polygon;
using geometry::Segment;
using geometry::Span;

// A minimum positive anchor size so a custom pad's required `(size W H)`
// field is never zero/negative even for a degenerate (single-point)
// outline -- 1 micron, well below any real copper feature.
constexpr int64_t MIN_ANCHOR_SIZE_NM = 1'000;

[[nodiscard]] std::string side_prefix(geometry::FootprintSide side) {
  return side == geometry::FootprintSide::Bottom ? "B." : "F.";
}

// A pad's `(layers ...)` field, synthesized from its single resolved
// copper layer -- geometry::Pad tracks only that one LayerRef, never the
// accompanying Mask/Paste layers a real SMD pad also needs, so this
// derives them by the standard F./B. naming convention (an Approximated-
// tier assumption: presence of the default mask/paste layers, not
// independently tracked). A copper layer whose name has neither prefix
// (never produced by import_stackup today, but not schema-forbidden)
// falls back to emitting just that one layer name.
[[nodiscard]] std::vector<SExpr> pad_layer_names(const std::string& copper_layer_name) {
  std::string prefix;
  if (copper_layer_name.starts_with("F.")) {
    prefix = "F.";
  } else if (copper_layer_name.starts_with("B.")) {
    prefix = "B.";
  }
  if (prefix.empty()) {
    return {str(copper_layer_name)};
  }
  return {str(copper_layer_name), str(prefix + "Mask"), str(prefix + "Paste")};
}

[[nodiscard]] SExpr property_node(const std::string& key,
                                  const std::string& value,
                                  const std::string& layer_name,
                                  core::EntityId footprint_id,
                                  const std::string& property_kind) {
  const std::string uuid_name =
      "pcbir:geometry:footprint_property:" + std::to_string(footprint_id.value()) + ":" +
      property_kind;
  return tagged("property",
                {str(key),
                 str(value),
                 tagged("at", {sym("0"), sym("0")}),
                 tagged("layer", {str(layer_name)}),
                 tagged("uuid", {str(uuid_v5(UUID_NAMESPACE, uuid_name))})});
}

// A custom pad's `(primitives (gr_poly (pts ...)))` points (each already
// relative to `pad_center`, matching how a real pcbnew-authored custom
// pad's primitives are anchored -- verified against real pcbnew output:
// a custom pad's own `(at x y)` rotation, left at 0 here, is applied to
// these primitive coordinates independently of the footprint's own
// rotation, so no rotation is needed to place them, only re-centering)
// plus the anchor pad's own nominal `(size W H)`, taken from the point
// list's own bounding box. An Arc span (produced by circle/oval/
// roundrect pad shapes) is flattened to chords first, since KiCad's
// gr_poly primitive supports only straight edges.
struct PadShapeExport {
  std::vector<SExpr> primitive_points;
  int64_t width_nm = 0;
  int64_t height_nm = 0;
};

[[nodiscard]] PadShapeExport build_custom_pad_shape(const Polygon& outline,
                                                    const Point& pad_center) {
  std::vector<Point> relative_points;
  for (const Span& span : outline.outline.spans) {
    if (const auto* segment = std::get_if<Segment>(&span)) {
      relative_points.push_back(segment->start - pad_center);
    } else {
      for (const Point& flattened : flatten_arc(std::get<Arc>(span))) {
        relative_points.push_back(flattened - pad_center);
      }
    }
  }
  if (relative_points.empty()) {
    throw ExportError("Pad outline has no spans");
  }

  Point min_point = relative_points.front();
  Point max_point = relative_points.front();
  std::vector<SExpr> points;
  points.reserve(relative_points.size());
  for (const Point& point : relative_points) {
    min_point.x = std::min(min_point.x, point.x);
    min_point.y = std::min(min_point.y, point.y);
    max_point.x = std::max(max_point.x, point.x);
    max_point.y = std::max(max_point.y, point.y);
    points.push_back(coordinate_pair("xy", point));
  }

  return PadShapeExport{.primitive_points = std::move(points),
                        .width_nm = std::max(max_point.x - min_point.x, MIN_ANCHOR_SIZE_NM),
                        .height_nm = std::max(max_point.y - min_point.y, MIN_ANCHOR_SIZE_NM)};
}

[[nodiscard]] SExpr export_smd_pad(const geometry::Pad& pad,
                                   const stackup::Layer& copper_layer,
                                   std::optional<SExpr> net,
                                   core::EntityId pad_id,
                                   const Point& footprint_position,
                                   int64_t footprint_rotation_e6) {
  const Point local_at =
      footprint_local_point(pad.position, footprint_position, footprint_rotation_e6);
  PadShapeExport shape = build_custom_pad_shape(pad.outline, pad.position);
  const std::string uuid_name = "pcbir:geometry:pad:" + std::to_string(pad_id.value());

  std::vector<SExpr> children{str(pad.pad_number), sym("smd"), sym("custom")};
  children.push_back(coordinate_pair("at", local_at));
  children.push_back(tagged(
      "size", {sym(format_nm_to_mm(shape.width_nm)), sym(format_nm_to_mm(shape.height_nm))}));
  children.push_back(tagged("layers", pad_layer_names(copper_layer.name)));
  push_if_present(children, std::move(net));
  children.push_back(
      tagged("options", {tagged("clearance", {sym("outline")}), tagged("anchor", {sym("rect")})}));
  children.push_back(tagged(
      "primitives",
      {tagged("gr_poly",
              {tagged("pts", std::move(shape.primitive_points)), tagged("width", {sym("0")})})}));
  children.push_back(tagged("uuid", {str(uuid_v5(UUID_NAMESPACE, uuid_name))}));

  return tagged("pad", std::move(children));
}

[[nodiscard]] SExpr export_thru_hole_pad(const geometry::Via& via,
                                         std::optional<SExpr> net,
                                         core::EntityId via_id,
                                         const Point& footprint_position,
                                         int64_t footprint_rotation_e6) {
  const Point local_at =
      footprint_local_point(via.position, footprint_position, footprint_rotation_e6);
  const std::string uuid_name = "pcbir:geometry:via:" + std::to_string(via_id.value());

  std::vector<SExpr> children{str(via.pad_number), sym("thru_hole"), sym("circle")};
  children.push_back(coordinate_pair("at", local_at));
  children.push_back(tagged(
      "size",
      {sym(format_nm_to_mm(via.pad_diameter_nm)), sym(format_nm_to_mm(via.pad_diameter_nm))}));
  children.push_back(tagged("drill", {sym(format_nm_to_mm(via.drill_diameter_nm))}));
  // Matches real pcbnew output for a plated through-hole pad with mask
  // clearance on both sides: paired F./B. layers collapse to a "*."
  // wildcard the same way "*.Cu" itself does (verified against real
  // pcbnew output).
  children.push_back(tagged("layers", {str("*.Cu"), str("*.Mask")}));
  push_if_present(children, std::move(net));
  children.push_back(tagged("uuid", {str(uuid_v5(UUID_NAMESPACE, uuid_name))}));

  return tagged("pad", std::move(children));
}

[[nodiscard]] SExpr
export_footprint_entry(const geometry::GeometrySnapshot& geometry,
                       const stackup::StackupSnapshot& stackup,
                       const connectivity::ConnectivitySnapshot& nets,
                       const std::map<core::EntityId::ValueType, core::EntityId>& pin_nets,
                       core::EntityId footprint_id,
                       const geometry::Footprint& footprint) {
  const std::string prefix = side_prefix(footprint.side);
  const std::string library_name =
      "PCBIR:" + (footprint.reference_designator.empty() ? std::string("FOOTPRINT")
                                                         : footprint.reference_designator);
  const std::string uuid_name = "pcbir:geometry:footprint:" + std::to_string(footprint_id.value());

  std::vector<SExpr> children{str(library_name)};
  children.push_back(tagged("layer", {str(prefix + "Cu")}));
  children.push_back(tagged("uuid", {str(uuid_v5(UUID_NAMESPACE, uuid_name))}));

  std::vector<SExpr> at_children{sym(format_nm_to_mm(footprint.position.x)),
                                 sym(format_nm_to_mm(footprint.position.y))};
  if (const std::optional<int64_t> kicad_angle_e6 =
          format_kicad_rotation_e6(footprint.rotation_e6)) {
    at_children.push_back(sym(format_e6_to_degrees(*kicad_angle_e6)));
  }
  children.push_back(tagged("at", std::move(at_children)));

  children.push_back(property_node(
      "Reference", footprint.reference_designator, prefix + "SilkS", footprint_id, "reference"));
  children.push_back(
      property_node("Value", footprint.value, prefix + "Fab", footprint_id, "value"));

  const auto& pad_table = geometry.table<geometry::Pad>();
  const auto& via_table = geometry.table<geometry::Via>();
  for (const core::EntityId member_id : footprint.pads) {
    const auto pin_it = pin_nets.find(member_id.value());
    const core::EntityId net_id = pin_it != pin_nets.end() ? pin_it->second : core::EntityId{};

    if (const geometry::Pad* pad = pad_table.try_get(pad_table.find(member_id)); pad != nullptr) {
      const stackup::Layer& copper_layer = require_layer(stackup, pad->layer);
      children.push_back(export_smd_pad(*pad,
                                        copper_layer,
                                        net_field(nets, net_id),
                                        member_id,
                                        footprint.position,
                                        footprint.rotation_e6));
      continue;
    }
    const geometry::Via* via = via_table.try_get(via_table.find(member_id));
    if (via == nullptr) {
      throw ExportError("Footprint references a pad id that is neither a Pad nor a Via");
    }
    children.push_back(export_thru_hole_pad(
        *via, net_field(nets, net_id), member_id, footprint.position, footprint.rotation_e6));
  }

  return tagged("footprint", std::move(children));
}

} // namespace

std::vector<SExpr> export_footprints(const geometry::GeometrySnapshot& geometry,
                                     const stackup::StackupSnapshot& stackup,
                                     const connectivity::ConnectivitySnapshot& nets) {
  const std::map<core::EntityId::ValueType, core::EntityId> pin_nets = build_pin_net_index(nets);

  std::vector<SExpr> nodes;
  geometry.table<geometry::Footprint>().for_each(
      [&](core::EntityId id, const geometry::Footprint& footprint) {
        nodes.push_back(export_footprint_entry(geometry, stackup, nets, pin_nets, id, footprint));
      });
  return nodes;
}

} // namespace pcbir::kicad
