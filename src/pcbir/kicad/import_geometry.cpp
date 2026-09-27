// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import_geometry.hpp"

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <variant>
#include <vector>

#include "sexpr_util.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Arc;
using geometry::ArcDirection;
using geometry::Point;
using geometry::Segment;
using geometry::Span;

[[nodiscard]] Point parse_coordinate_pair(const SExpr& node) {
  if (node.children.size() < 3) {
    throw ImportError("malformed coordinate entry: expected (tag X Y)");
  }
  return Point{.x = parse_mm_to_nm(node.children.at(1).text),
               .y = parse_mm_to_nm(node.children.at(2).text)};
}

// Converts KiCad's three-point (start/mid/end) arc into PCB-IR's
// endpoints+center form via the standard circumcenter formula, computed
// in double and rounded to the nearest nanometre -- see import_geometry.hpp
// for why this can't be exact in general.
[[nodiscard]] Arc
kicad_three_point_arc_to_pcbir(const Point& start, const Point& mid, const Point& end) {
  const auto ax = static_cast<double>(start.x);
  const auto ay = static_cast<double>(start.y);
  const auto bx = static_cast<double>(mid.x);
  const auto by = static_cast<double>(mid.y);
  const auto cx = static_cast<double>(end.x);
  const auto cy = static_cast<double>(end.y);

  const double d = 2.0 * ((ax * (by - cy)) + (bx * (cy - ay)) + (cx * (ay - by)));
  if (d == 0.0) {
    throw ImportError("degenerate (gr_arc ...): start/mid/end are collinear");
  }

  const double a_sq = (ax * ax) + (ay * ay);
  const double b_sq = (bx * bx) + (by * by);
  const double c_sq = (cx * cx) + (cy * cy);
  const double center_x = ((a_sq * (by - cy)) + (b_sq * (cy - ay)) + (c_sq * (ay - by))) / d;
  const double center_y = ((a_sq * (cx - bx)) + (b_sq * (ax - cx)) + (c_sq * (bx - ax))) / d;
  const Point center{.x = static_cast<int64_t>(std::llround(center_x)),
                     .y = static_cast<int64_t>(std::llround(center_y))};

  // `mid` sits at exactly half the intended sweep, so comparing its
  // counterclockwise offset from `start` against `end`'s counterclockwise
  // offset from `start` is well-defined regardless of whether the total
  // sweep exceeds 180 degrees (unlike comparing start/end alone).
  const auto angle_of = [&center](const Point& p) {
    return std::atan2(static_cast<double>(p.y - center.y), static_cast<double>(p.x - center.x));
  };
  const auto normalize_ccw = [](double angle) {
    constexpr double two_pi = 2.0 * std::numbers::pi;
    while (angle < 0.0) {
      angle += two_pi;
    }
    while (angle >= two_pi) {
      angle -= two_pi;
    }
    return angle;
  };
  const double start_angle = angle_of(start);
  const double ccw_to_mid = normalize_ccw(angle_of(mid) - start_angle);
  const double ccw_to_end = normalize_ccw(angle_of(end) - start_angle);
  const ArcDirection direction =
      (ccw_to_mid < ccw_to_end) ? ArcDirection::CounterClockwise : ArcDirection::Clockwise;

  return Arc{.start = start, .end = end, .center = center, .direction = direction};
}

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

[[nodiscard]] Span reverse_span(const Span& span) {
  if (const auto* segment = std::get_if<Segment>(&span)) {
    return Span{Segment{.start = segment->end, .end = segment->start}};
  }
  const auto& arc = std::get<Arc>(span);
  const ArcDirection reversed = arc.direction == ArcDirection::Clockwise
                                    ? ArcDirection::CounterClockwise
                                    : ArcDirection::Clockwise;
  return Span{Arc{.start = arc.end, .end = arc.start, .center = arc.center, .direction = reversed}};
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

// PCB-IR's Polygon requires a counterclockwise outline
// (geometry/polygon.hpp -- validate()); KiCad's authored winding order is
// not guaranteed, so a clockwise loop is reversed (spans, and their
// order) to match.
[[nodiscard]] geometry::Contour canonicalize_winding(geometry::Contour contour) {
  if (geometry::orientation(contour) == geometry::Orientation::Clockwise) {
    std::ranges::reverse(contour.spans);
    for (Span& span : contour.spans) {
      span = reverse_span(span);
    }
  }
  return contour;
}

} // namespace

geometry::GeometrySnapshot import_board_outline(const SExpr& kicad_pcb,
                                                const stackup::StackupSnapshot& stackup) {
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

  geometry::GeometryWorkspace workspace;
  for (geometry::Contour& loop : assemble_loops(std::move(edge_spans))) {
    workspace.insert(geometry::BoardOutline{
        .outline = geometry::Polygon{.outline = canonicalize_winding(std::move(loop)), .holes = {}},
        .layer = edge_cuts_layer_id});
  }

  return workspace.commit();
}

} // namespace pcbir::kicad
