// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import_zone.hpp"

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "coordinate_util.hpp"
#include "sexpr_util.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Contour;
using geometry::Point;
using geometry::Polygon;
using geometry::Segment;
using geometry::Span;

// Parses one `(polygon (pts (xy X Y) (xy X Y) ...))` node's `(pts ...)`
// into a closed straight-edge Contour, in file order -- winding is the
// caller's job (Polygon requires the outer boundary CounterClockwise but
// every hole Clockwise, so there's no single "canonical" winding this
// function alone could produce).
[[nodiscard]] Contour parse_zone_polygon(const SExpr& polygon_node) {
  const SExpr* pts_node = find_child(polygon_node, "pts");
  if (pts_node == nullptr || pts_node->children.size() < 4) {
    throw ImportError("malformed (zone ...): (polygon (pts ...)) needs at least 3 points");
  }

  std::vector<Point> points;
  points.reserve(pts_node->children.size() - 1);
  for (std::size_t i = 1; i < pts_node->children.size(); ++i) {
    const SExpr& entry = pts_node->children.at(i);
    if (!entry.is_list() || entry.children.empty() || !entry.children.front().is_symbol() ||
        entry.children.front().text != "xy") {
      // An arc-cornered zone outline (a `(arc ...)` entry alongside
      // `(xy ...)` ones) is Unsupported for this pass
      // (docs/rfcs/0003-kicad-importer-exporter.md).
      throw ImportError("unsupported (zone ...) (pts ...) entry: expected only (xy X Y)");
    }
    points.push_back(parse_coordinate_pair(entry));
  }

  std::vector<Span> spans;
  spans.reserve(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    spans.emplace_back(Segment{.start = points.at(i), .end = points.at((i + 1) % points.size())});
  }
  return Contour{.spans = std::move(spans)};
}

// canonicalize_winding (coordinate_util.hpp) always produces
// CounterClockwise, the winding an outline needs; a hole needs the
// opposite (Clockwise -- Polygon::validate()'s contract), so this reverses
// whatever canonicalize_winding produces.
[[nodiscard]] Contour canonicalize_hole_winding(Contour contour) {
  contour = canonicalize_winding(std::move(contour));
  std::ranges::reverse(contour.spans);
  for (Span& span : contour.spans) {
    span = reverse_span(span);
  }
  return contour;
}

[[nodiscard]] Polygon parse_zone_outline(const SExpr& zone) {
  const std::vector<const SExpr*> polygon_nodes = find_all_children(zone, "polygon");
  if (polygon_nodes.empty()) {
    throw ImportError("malformed (zone ...): missing (polygon (pts ...))");
  }

  Polygon outline;
  outline.outline = canonicalize_winding(parse_zone_polygon(*polygon_nodes.front()));
  outline.holes.reserve(polygon_nodes.size() - 1);
  for (std::size_t i = 1; i < polygon_nodes.size(); ++i) {
    outline.holes.push_back(canonicalize_hole_winding(parse_zone_polygon(*polygon_nodes.at(i))));
  }
  return outline;
}

// A zone's `(layer "X")` (single-layer) or `(layers "X" "Y" ...)`
// (multi-layer, poured onto every listed layer at once) field, resolved
// to the Layer EntityId(s) that name(s).
[[nodiscard]] std::vector<core::EntityId>
resolve_zone_layers(const SExpr& zone, const stackup::StackupSnapshot& stackup) {
  if (const SExpr* layer_node = find_child(zone, "layer"); layer_node != nullptr) {
    return {resolve_required_layer(zone, stackup)};
  }
  const SExpr* layers_node = find_child(zone, "layers");
  if (layers_node == nullptr) {
    throw ImportError("malformed (zone ...): missing (layer ...)/(layers ...)");
  }
  std::vector<core::EntityId> ids;
  for (const std::string& name : parse_layer_names(*layers_node)) {
    const core::EntityId id = find_layer_id_by_name(stackup, name);
    if (id.is_null()) {
      throw ImportError("(zone ...) references an unrecognized layer '" + name + "'");
    }
    ids.push_back(id);
  }
  return ids;
}

} // namespace

geometry::GeometrySnapshot import_zones(const SExpr& kicad_pcb,
                                        const stackup::StackupSnapshot& stackup,
                                        const connectivity::ConnectivitySnapshot& nets) {
  const std::map<std::string, core::EntityId> net_index = build_net_index(nets);

  geometry::GeometryWorkspace workspace;
  for (const SExpr* zone : find_all_children(kicad_pcb, "zone")) {
    const Polygon outline = parse_zone_outline(*zone);
    const core::EntityId net_id = resolve_optional_net(*zone, net_index);
    for (const core::EntityId layer_id : resolve_zone_layers(*zone, stackup)) {
      workspace.insert(geometry::CopperPour{.outline = outline, .layer = layer_id, .net = net_id});
    }
  }
  return workspace.commit();
}

} // namespace pcbir::kicad
