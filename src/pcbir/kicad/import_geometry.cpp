// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import_geometry.hpp"

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "coordinate_util.hpp"
#include "sexpr_util.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Point;
using geometry::Segment;
using geometry::Span;

[[nodiscard]] Span parse_edge_span(const SExpr& row) {
  const SExpr* start_node = find_child(row, "start");
  const SExpr* end_node = find_child(row, "end");
  if (start_node == nullptr || end_node == nullptr) {
    throw ImportError("malformed Edge.Cuts entry: missing (start ...)/(end ...)");
  }
  const Point start = parse_coordinate_pair(*start_node);
  const Point end = parse_coordinate_pair(*end_node);

  if (!row.children.empty() && row.children.front().text == "gr_arc") {
    const SExpr* mid_node = find_child(row, "mid");
    if (mid_node == nullptr) {
      throw ImportError("malformed (gr_arc ...): missing (mid ...)");
    }
    return Span{kicad_three_point_arc_to_pcbir(start, parse_coordinate_pair(*mid_node), end)};
  }
  return Span{Segment{.start = start, .end = end}};
}

[[nodiscard]] bool is_on_edge_cuts(const SExpr& row) {
  const SExpr* layer_node = find_child(row, "layer");
  return layer_node != nullptr && layer_node->children.size() >= 2 &&
         layer_node->children.at(1).text == "Edge.Cuts";
}

// Chains loose edge spans into one or more closed loops by matching
// shared endpoints (exact equality -- adjacent edges in a real
// KiCad-authored outline share bit-identical vertices, so no tolerance is
// needed). KiCad doesn't guarantee edges are authored in any particular
// order or direction, so a span may need reversing to attach.
[[nodiscard]] std::vector<geometry::Contour> assemble_loops(std::vector<Span> spans) {
  std::vector<geometry::Contour> loops;
  while (!spans.empty()) {
    std::vector<Span> loop;
    loop.push_back(spans.back());
    spans.pop_back();

    while (geometry::span_end(loop.back()) != geometry::span_start(loop.front())) {
      const Point target = geometry::span_end(loop.back());
      auto it = std::ranges::find_if(
          spans, [&](const Span& s) { return geometry::span_start(s) == target; });
      bool reversed = false;
      if (it == spans.end()) {
        it = std::ranges::find_if(spans,
                                  [&](const Span& s) { return geometry::span_end(s) == target; });
        reversed = true;
      }
      if (it == spans.end()) {
        throw ImportError("Edge.Cuts outline has a dangling or open edge (no closed loop)");
      }
      loop.push_back(reversed ? reverse_span(*it) : *it);
      spans.erase(it);
    }

    loops.push_back(geometry::Contour{.spans = std::move(loop)});
  }
  return loops;
}

} // namespace

geometry::GeometrySnapshot import_board_outline(const SExpr& kicad_pcb,
                                                const stackup::StackupSnapshot& stackup,
                                                const geometry::GeometrySnapshot& base) {
  core::EntityId edge_cuts_layer_id;
  stackup.table<stackup::Layer>().for_each([&](core::EntityId id, const stackup::Layer& layer) {
    if (layer.kind == stackup::LayerKind::EdgeCuts) {
      edge_cuts_layer_id = id;
    }
  });
  if (edge_cuts_layer_id.is_null()) {
    throw ImportError("stackup has no Edge.Cuts layer; import_stackup must run first");
  }

  std::vector<Span> edge_spans;
  for (const char* tag : {"gr_line", "gr_arc"}) {
    for (const SExpr* row : find_all_children(kicad_pcb, tag)) {
      if (is_on_edge_cuts(*row)) {
        edge_spans.push_back(parse_edge_span(*row));
      }
    }
  }

  geometry::GeometryWorkspace workspace(base);
  for (geometry::Contour& loop : assemble_loops(std::move(edge_spans))) {
    workspace.insert(geometry::BoardOutline{
        .outline = geometry::Polygon{.outline = canonicalize_winding(std::move(loop)), .holes = {}},
        .layer = edge_cuts_layer_id});
  }

  return workspace.commit();
}

} // namespace pcbir::kicad
