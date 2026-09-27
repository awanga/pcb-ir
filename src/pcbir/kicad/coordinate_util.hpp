// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_COORDINATE_UTIL_HPP
#define PCBIR_KICAD_COORDINATE_UTIL_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <variant>

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

// Parses an optional trailing rotation field (a `(at x y [ANGLE])` node's
// 4th child, or absent -- KiCad omits the field entirely for a 0-degree
// rotation) and converts it from KiCad's clockwise-positive on-disk
// convention to geometry::rotate's counterclockwise-positive convention
// (pcbir/geometry/rotate.hpp) by negating it -- verified against real
// pcbnew output: rotating a footprint by a stored angle of +90 moves a
// point at local (+X, 0) to absolute (0, -X), which is what
// geometry::rotate produces only for an angle of -90
// (docs/rfcs/0003-kicad-importer-exporter.md). Every KiCad-stored angle
// (footprint orientation, pad/via local delta) uses this same convention,
// so every caller that feeds an angle to geometry::rotate must negate it
// exactly once, here.
[[nodiscard]] inline int64_t parse_kicad_rotation_e6(const SExpr& at_node) {
  if (at_node.children.size() < 4) {
    return 0;
  }
  return -parse_degrees_to_e6(at_node.children.at(3).text);
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

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_COORDINATE_UTIL_HPP
