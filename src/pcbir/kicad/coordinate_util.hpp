// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_COORDINATE_UTIL_HPP
#define PCBIR_KICAD_COORDINATE_UTIL_HPP

#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numbers>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "sexpr_util.hpp"

// Internal geometry-conversion helpers shared by every importer piece that
// places KiCad coordinates/angles/layer references (import_geometry.cpp,
// import_footprint.cpp, import_track.cpp, import_zone.cpp, ...) -- not part
// of the public API, so this lives under src/, mirroring sexpr_util.hpp's
// own precedent.
namespace pcbir::kicad {

// Parses a `(tag X Y)` node (e.g. `(start 1.5 2)`, `(xy 0 0)`, `(at -1 0)`'s
// first two fields) into a Point.
[[nodiscard]] inline geometry::Point parse_coordinate_pair(const SExpr& node) {
  if (node.children.size() < 3) {
    throw ImportError("malformed coordinate entry: expected (tag X Y)");
  }
  return geometry::Point{.x = parse_mm_to_nm(node.children.at(1).text),
                         .y = parse_mm_to_nm(node.children.at(2).text)};
}

// Converts KiCad's three-point (start/mid/end) arc into PCB-IR's
// endpoints+center form via the standard circumcenter formula, computed in
// double and rounded to the nearest nanometre -- see
// pcbir/kicad/import_geometry.hpp for why this can't be exact in general.
// Shared by every KiCad entity that uses this same three-point
// parameterization (gr_arc, track arc segments, ...).
[[nodiscard]] inline geometry::Arc kicad_three_point_arc_to_pcbir(const geometry::Point& start,
                                                                  const geometry::Point& mid,
                                                                  const geometry::Point& end) {
  const auto ax = static_cast<double>(start.x);
  const auto ay = static_cast<double>(start.y);
  const auto bx = static_cast<double>(mid.x);
  const auto by = static_cast<double>(mid.y);
  const auto cx = static_cast<double>(end.x);
  const auto cy = static_cast<double>(end.y);

  const double d = 2.0 * ((ax * (by - cy)) + (bx * (cy - ay)) + (cx * (ay - by)));
  if (d == 0.0) {
    throw ImportError("degenerate three-point arc: start/mid/end are collinear");
  }

  const double a_sq = (ax * ax) + (ay * ay);
  const double b_sq = (bx * bx) + (by * by);
  const double c_sq = (cx * cx) + (cy * cy);
  const double center_x = ((a_sq * (by - cy)) + (b_sq * (cy - ay)) + (c_sq * (ay - by))) / d;
  const double center_y = ((a_sq * (cx - bx)) + (b_sq * (ax - cx)) + (c_sq * (bx - ax))) / d;
  const geometry::Point center{.x = static_cast<int64_t>(std::llround(center_x)),
                               .y = static_cast<int64_t>(std::llround(center_y))};

  // `mid` sits at exactly half the intended sweep, so comparing its
  // counterclockwise offset from `start` against `end`'s counterclockwise
  // offset from `start` is well-defined regardless of whether the total
  // sweep exceeds 180 degrees (unlike comparing start/end alone).
  const auto angle_of = [&center](const geometry::Point& p) {
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
  const geometry::ArcDirection direction = (ccw_to_mid < ccw_to_end)
                                               ? geometry::ArcDirection::CounterClockwise
                                               : geometry::ArcDirection::Clockwise;

  return geometry::Arc{.start = start, .end = end, .center = center, .direction = direction};
}

// Computes the midpoint KiCad's three-point (start/mid/end) arc
// parameterization needs for `arc`, the exporter's inverse of
// kicad_three_point_arc_to_pcbir (above): a point exactly halfway around
// `arc`'s sweep from start to end, in `arc.direction`'s own sense of
// travel, so re-parsing the emitted (start/mid/end) triple recovers the
// same direction. Computed in double and rounded to the nearest nanometre
// (half away from zero), the same documented best-effort precision
// contract as kicad_three_point_arc_to_pcbir and geometry::rotate's
// non-90-degree fallback -- an arc's true midpoint is not exactly
// representable in integer coordinates in general.
[[nodiscard]] inline geometry::Point arc_three_point_mid(const geometry::Arc& arc) {
  const auto center_x = static_cast<double>(arc.center.x);
  const auto center_y = static_cast<double>(arc.center.y);
  const double start_x = static_cast<double>(arc.start.x) - center_x;
  const double start_y = static_cast<double>(arc.start.y) - center_y;
  const double end_x = static_cast<double>(arc.end.x) - center_x;
  const double end_y = static_cast<double>(arc.end.y) - center_y;

  const double radius = std::hypot(start_x, start_y);
  const double start_angle = std::atan2(start_y, start_x);
  const double end_angle = std::atan2(end_y, end_x);

  constexpr double two_pi = 2.0 * std::numbers::pi;
  double sweep = end_angle - start_angle;
  if (arc.direction == geometry::ArcDirection::CounterClockwise) {
    while (sweep <= 0.0) {
      sweep += two_pi; // (0, 2*pi]; a full circle (start == end) lands on 2*pi.
    }
  } else {
    while (sweep >= 0.0) {
      sweep -= two_pi; // [-2*pi, 0); a full circle (start == end) lands on -2*pi.
    }
  }

  const double mid_angle = start_angle + (sweep / 2.0);
  return geometry::Point{
      .x = static_cast<int64_t>(std::llround(center_x + (radius * std::cos(mid_angle)))),
      .y = static_cast<int64_t>(std::llround(center_y + (radius * std::sin(mid_angle))))};
}

// Parses an optional trailing rotation field (a `(at x y [ANGLE])` node's
// 4th child, or absent -- KiCad omits the field entirely for a 0-degree
// rotation) and converts it from KiCad's clockwise-positive on-disk
// convention to geometry::rotate's counterclockwise-positive convention
// (pcbir/geometry/rotate.hpp) by negating it -- verified against real
// pcbnew output: rotating a footprint by a stored angle of +90 moves a
// point at local (+X, 0) to absolute (0, -X), which is what
// geometry::rotate produces only for an angle of -90
// (docs/rfcs/0003-kicad-importer-exporter.md). Every KiCad-stored angle
// (a footprint's own orientation, or a pad/via's own -- both are written
// as fully-resolved absolute angles, verified against real pcbnew output;
// a pad's stored angle never needs its parent footprint's angle added)
// uses this same convention, so every caller that feeds an angle to
// geometry::rotate must negate it exactly once, here. The result is
// normalized to [0, 360_000_000) so every caller sees a canonical range
// regardless of the sign of the value KiCad wrote.
[[nodiscard]] inline int64_t parse_kicad_rotation_e6(const SExpr& at_node) {
  if (at_node.children.size() < 4) {
    return 0;
  }
  constexpr int64_t degrees_e6_per_full_turn = 360'000'000;
  int64_t angle_e6 = -parse_degrees_to_e6(at_node.children.at(3).text) % degrees_e6_per_full_turn;
  if (angle_e6 < 0) {
    angle_e6 += degrees_e6_per_full_turn;
  }
  return angle_e6;
}

// Reverses a Span's direction of travel (swaps a Segment's endpoints, or
// swaps an Arc's endpoints and flips its sweep direction) without changing
// the physical curve it describes -- used both to attach a loose edge to a
// loop in whichever orientation matches (import_geometry.cpp's
// assemble_loops) and to reverse an already-assembled loop
// (canonicalize_winding, below).
[[nodiscard]] inline geometry::Span reverse_span(const geometry::Span& span) {
  if (const auto* segment = std::get_if<geometry::Segment>(&span)) {
    return geometry::Span{geometry::Segment{.start = segment->end, .end = segment->start}};
  }
  const auto& arc = std::get<geometry::Arc>(span);
  const geometry::ArcDirection reversed = arc.direction == geometry::ArcDirection::Clockwise
                                              ? geometry::ArcDirection::CounterClockwise
                                              : geometry::ArcDirection::Clockwise;
  return geometry::Span{geometry::Arc{
      .start = arc.end, .end = arc.start, .center = arc.center, .direction = reversed}};
}

// PCB-IR's Polygon requires a counterclockwise outline
// (geometry/polygon.hpp -- validate()); KiCad's authored winding order
// (and this module's own programmatically-constructed pad-shape outlines)
// isn't guaranteed to already be one, so a clockwise loop is reversed
// (spans, order, and each span's own direction) to match.
[[nodiscard]] inline geometry::Contour canonicalize_winding(geometry::Contour contour) {
  if (geometry::orientation(contour) == geometry::Orientation::Clockwise) {
    std::ranges::reverse(contour.spans);
    for (geometry::Span& span : contour.spans) {
      span = reverse_span(span);
    }
  }
  return contour;
}

// Translates every point of `span` by `delta`, without changing its shape
// (a rotated-then-translated pad/via outline is built by rotating a
// pad-local, origin-centered shape in place via geometry::rotate and then
// translating the result to its final absolute position with this).
[[nodiscard]] inline geometry::Span translate(const geometry::Span& span,
                                              const geometry::Point& delta) {
  if (const auto* segment = std::get_if<geometry::Segment>(&span)) {
    return geometry::Span{
        geometry::Segment{.start = segment->start + delta, .end = segment->end + delta}};
  }
  const auto& arc = std::get<geometry::Arc>(span);
  return geometry::Span{geometry::Arc{.start = arc.start + delta,
                                      .end = arc.end + delta,
                                      .center = arc.center + delta,
                                      .direction = arc.direction}};
}

[[nodiscard]] inline geometry::Contour translate(const geometry::Contour& contour,
                                                 const geometry::Point& delta) {
  geometry::Contour result;
  result.spans.reserve(contour.spans.size());
  for (const geometry::Span& span : contour.spans) {
    result.spans.push_back(translate(span, delta));
  }
  return result;
}

[[nodiscard]] inline geometry::Polygon translate(const geometry::Polygon& polygon,
                                                 const geometry::Point& delta) {
  geometry::Polygon result;
  result.outline = translate(polygon.outline, delta);
  result.holes.reserve(polygon.holes.size());
  for (const geometry::Contour& hole : polygon.holes) {
    result.holes.push_back(translate(hole, delta));
  }
  return result;
}

// Finds the stackup Layer whose name is exactly `name`, or a null EntityId
// if no such layer was imported (e.g. import_stackup doesn't recognize the
// requested layer's LayerKind).
[[nodiscard]] inline core::EntityId find_layer_id_by_name(const stackup::StackupSnapshot& stackup,
                                                          const std::string& name) {
  core::EntityId result;
  stackup.table<stackup::Layer>().for_each([&](core::EntityId id, const stackup::Layer& layer) {
    if (layer.name == name) {
      result = id;
    }
  });
  return result;
}

// Parses a `(layers "A" "B" ...)` node's names (its own leading Symbol
// child, "layers", skipped) -- shared by a pad's/via's multi-layer
// `(layers ...)` field, distinct from a single-layer `(layer "NAME")`
// field (resolve_required_layer, below).
[[nodiscard]] inline std::vector<std::string> parse_layer_names(const SExpr& layers_node) {
  std::vector<std::string> names;
  names.reserve(layers_node.children.size());
  for (std::size_t i = 1; i < layers_node.children.size(); ++i) {
    names.push_back(layers_node.children.at(i).text);
  }
  return names;
}

[[nodiscard]] inline const stackup::LayerStack&
require_layer_stack(const stackup::StackupSnapshot& stackup) {
  const stackup::LayerStack* stack = nullptr;
  stackup.table<stackup::LayerStack>().for_each(
      [&](core::EntityId, const stackup::LayerStack& candidate) { stack = &candidate; });
  if (stack == nullptr || stack->layers.empty()) {
    throw ImportError("via/thru-hole pad requires a non-empty stackup LayerStack");
  }
  return *stack;
}

[[nodiscard]] inline std::size_t layer_stack_position(const stackup::LayerStack& stack,
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
// order finding) or exactly 2 explicit copper layer names. Shared by
// import_footprint.cpp (thru-hole pads) and the free-standing `(via ...)`
// importer, which resolve this identically.
[[nodiscard]] inline std::pair<core::EntityId, core::EntityId>
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

// Finds the stackup Layer a node's own `(layer "NAME")` child (a track
// segment/arc's, a zone's, a free via's single-layer field -- not the
// multi-layer `(layers ...)` a pad/via-as-pad uses, see
// resolve_via_layer_span, above) refers to.
[[nodiscard]] inline core::EntityId
resolve_required_layer(const SExpr& node, const stackup::StackupSnapshot& stackup) {
  const SExpr* layer_node = find_child(node, "layer");
  if (layer_node == nullptr || layer_node->children.size() < 2) {
    throw ImportError("malformed entry: missing (layer \"NAME\")");
  }
  const core::EntityId id = find_layer_id_by_name(stackup, layer_node->children.at(1).text);
  if (id.is_null()) {
    throw ImportError("entry references an unrecognized layer '" + layer_node->children.at(1).text +
                      "'");
  }
  return id;
}

// A name -> EntityId index over every Net in `nets`, built once so
// resolve_optional_net can look up a `(net "NAME")` reference in O(log n)
// per pad/track/via/zone rather than re-scanning the whole Net table each
// time.
[[nodiscard]] inline std::map<std::string, core::EntityId>
build_net_index(const connectivity::ConnectivitySnapshot& nets) {
  std::map<std::string, core::EntityId> index;
  nets.table<connectivity::Net>().for_each(
      [&](core::EntityId id, const connectivity::Net& net) { index.emplace(net.name, id); });
  return index;
}

// `node`'s own `(net "NAME")` child resolved against `net_index`, or a
// null EntityId for an unconnected entity (no such child at all -- KiCad
// omits it entirely rather than writing a placeholder, verified against
// real pcbnew output). Every name present must already be in `net_index`
// since import_nets scans the same tree for every `(net "NAME")`
// occurrence up front; a name that isn't found means the caller passed a
// `nets` snapshot built from different input than `node` came from.
[[nodiscard]] inline core::EntityId
resolve_optional_net(const SExpr& node, const std::map<std::string, core::EntityId>& net_index) {
  const SExpr* net_node = find_child(node, "net");
  if (net_node == nullptr || net_node->children.size() < 2) {
    return core::EntityId{};
  }
  const auto it = net_index.find(net_node->children.at(1).text);
  if (it == net_index.end()) {
    throw ImportError("entry references net '" + net_node->children.at(1).text +
                      "' that import_nets did not find");
  }
  return it->second;
}

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_COORDINATE_UTIL_HPP
