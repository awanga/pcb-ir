// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import_track.hpp"

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/path.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <map>
#include <string>

#include "coordinate_util.hpp"
#include "sexpr_util.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Segment;
using geometry::Span;

[[nodiscard]] int64_t parse_required_width(const SExpr& node) {
  const SExpr* width_node = find_child(node, "width");
  if (width_node == nullptr || width_node->children.size() < 2) {
    throw ImportError("malformed track entry: missing (width W)");
  }
  return parse_mm_to_nm(width_node->children.at(1).text);
}

[[nodiscard]] Span parse_segment_span(const SExpr& node) {
  const SExpr* start_node = find_child(node, "start");
  const SExpr* end_node = find_child(node, "end");
  if (start_node == nullptr || end_node == nullptr) {
    throw ImportError("malformed (segment ...): missing (start ...)/(end ...)");
  }
  return Span{Segment{.start = parse_coordinate_pair(*start_node),
                      .end = parse_coordinate_pair(*end_node)}};
}

[[nodiscard]] Span parse_track_arc_span(const SExpr& node) {
  const SExpr* start_node = find_child(node, "start");
  const SExpr* mid_node = find_child(node, "mid");
  const SExpr* end_node = find_child(node, "end");
  if (start_node == nullptr || mid_node == nullptr || end_node == nullptr) {
    throw ImportError("malformed (arc ...): missing (start ...)/(mid ...)/(end ...)");
  }
  return Span{kicad_three_point_arc_to_pcbir(parse_coordinate_pair(*start_node),
                                             parse_coordinate_pair(*mid_node),
                                             parse_coordinate_pair(*end_node))};
}

void import_track_entry(const SExpr& node,
                        const Span& span,
                        const stackup::StackupSnapshot& stackup,
                        const std::map<std::string, core::EntityId>& net_index,
                        geometry::GeometryWorkspace& workspace) {
  workspace.insert(geometry::Track{
      .path = geometry::Path{.spans = {geometry::WidthSpan{
                                 .geometry = span, .width_nm = parse_required_width(node)}}},
      .layer = resolve_required_layer(node, stackup),
      .net = resolve_optional_net(node, net_index)});
}

} // namespace

geometry::GeometrySnapshot import_tracks(const SExpr& kicad_pcb,
                                         const stackup::StackupSnapshot& stackup,
                                         const connectivity::ConnectivitySnapshot& nets) {
  const std::map<std::string, core::EntityId> net_index = build_net_index(nets);

  geometry::GeometryWorkspace workspace;
  for (const SExpr* row : find_all_children(kicad_pcb, "segment")) {
    import_track_entry(*row, parse_segment_span(*row), stackup, net_index, workspace);
  }
  for (const SExpr* row : find_all_children(kicad_pcb, "arc")) {
    import_track_entry(*row, parse_track_arc_span(*row), stackup, net_index, workspace);
  }
  return workspace.commit();
}

} // namespace pcbir::kicad
