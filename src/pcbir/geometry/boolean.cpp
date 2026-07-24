// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/boolean.hpp"

#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>
#include <variant>
#include <vector>

// This is Clipper2's own umbrella header (its symbols are physically
// declared across several finer-grained sub-headers that misc-include-
// cleaner would rather see named individually, but every consumer --
// including Clipper2's own generated code -- includes exactly this one).
// NOLINTNEXTLINE(misc-include-cleaner)
#include <clipper2/clipper.h>

namespace pcbir::geometry {

namespace {

// NOLINTNEXTLINE(misc-include-cleaner)
using ClipperPath = Clipper2Lib::Path64;
// NOLINTNEXTLINE(misc-include-cleaner)
using ClipperPaths = Clipper2Lib::Paths64;

constexpr double PI = std::numbers::pi;

double angle_of(const Point& center, const Point& p) {
  return std::atan2(static_cast<double>(p.y - center.y), static_cast<double>(p.x - center.x));
}

// Sweep angle (radians, in (0, 2*PI]) traversed from `arc.start` to
// `arc.end` going around in `arc.direction`. A coincident start/end always
// means a full circle (Arc's own documented degenerate case), not a
// zero-length arc.
double sweep_angle(const Arc& arc) {
  if (arc.start == arc.end) {
    return 2 * PI;
  }
  const double start_angle = angle_of(arc.center, arc.start);
  const double end_angle = angle_of(arc.center, arc.end);
  if (arc.direction == ArcDirection::CounterClockwise) {
    double diff = end_angle - start_angle;
    if (diff <= 0) {
      diff += 2 * PI;
    }
    return diff;
  }
  double diff = start_angle - end_angle;
  if (diff <= 0) {
    diff += 2 * PI;
  }
  return diff;
}

int arc_segment_count(double sweep) {
  const double fraction = sweep / (2 * PI);
  const int segments = static_cast<int>(std::lround(fraction * ARC_FLATTEN_SEGMENTS_PER_FULL_TURN));
  return std::max(segments, 1);
}

// Appends the interior points of `arc` (excluding both `arc.start`, which
// the caller already added as the previous span's contribution, and
// `arc.end`, which is either the next span's contribution or the implicit
// closing point) approximating the curve with `arc_segment_count(sweep)`
// straight chords.
void append_arc_interior_points(const Arc& arc, std::vector<Point>& out) {
  const double radius = std::hypot(static_cast<double>(arc.start.x - arc.center.x),
                                   static_cast<double>(arc.start.y - arc.center.y));
  if (radius == 0.0) {
    return;
  }
  const double start_angle = angle_of(arc.center, arc.start);
  const double sweep = sweep_angle(arc);
  const double signed_sweep = (arc.direction == ArcDirection::CounterClockwise) ? sweep : -sweep;
  const int segments = arc_segment_count(sweep);
  for (int i = 1; i < segments; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(segments);
    const double angle = start_angle + (signed_sweep * t);
    out.push_back(Point{
        .x = static_cast<int64_t>(
            std::llround(static_cast<double>(arc.center.x) + (radius * std::cos(angle)))),
        .y = static_cast<int64_t>(
            std::llround(static_cast<double>(arc.center.y) + (radius * std::sin(angle)))),
    });
  }
}

ClipperPath contour_to_clipper_path(const Contour& contour) {
  std::vector<Point> points;
  points.reserve(contour.spans.size());
  for (const Span& span : contour.spans) {
    points.push_back(span_start(span));
    if (std::holds_alternative<Arc>(span)) {
      append_arc_interior_points(std::get<Arc>(span), points);
    }
  }
  ClipperPath path;
  path.reserve(points.size());
  for (const Point& p : points) {
    path.emplace_back(p.x, p.y);
  }
  return path;
}

void append_polygon_paths(const Polygon& polygon, ClipperPaths& out) {
  out.push_back(contour_to_clipper_path(polygon.outline));
  for (const Contour& hole : polygon.holes) {
    out.push_back(contour_to_clipper_path(hole));
  }
}

ClipperPaths polygons_to_clipper_paths(const std::vector<Polygon>& polygons) {
  ClipperPaths result;
  for (const Polygon& polygon : polygons) {
    append_polygon_paths(polygon, result);
  }
  return result;
}

Contour clipper_path_to_contour(const ClipperPath& path) {
  Contour contour;
  contour.spans.reserve(path.size());
  for (std::size_t i = 0; i < path.size(); ++i) {
    // `i` is bounds-checked by the loop condition itself.
    // NOLINTNEXTLINE(*-avoid-unchecked-container-access)
    const auto& start = path[i];
    // `(i + 1) % path.size()` is always in [0, path.size()).
    // NOLINTNEXTLINE(*-avoid-unchecked-container-access)
    const auto& end = path[(i + 1) % path.size()];
    contour.spans.emplace_back(Segment{
        .start = Point{.x = start.x, .y = start.y},
        .end = Point{.x = end.x, .y = end.y},
    });
  }
  return contour;
}

// Walks one outer (non-hole) node of Clipper2's solution tree, building a
// Polygon from it: the node's own ring is the outline, its direct children
// are holes, and each hole's own children (islands nested inside a hole)
// recurse as further, separate top-level Polygons -- PolyTree64's
// outer/hole/outer/... alternation is a structural guarantee of Clipper2's
// solution construction, not just a naming convention. Recursion depth
// tracks the outline/hole/island nesting depth of the boolean result, which
// is bounded by the (small, finite) number of input contours -- not
// user-controlled unbounded input.
// NOLINTNEXTLINE(misc-no-recursion, misc-include-cleaner)
void collect_outer_polygon(const Clipper2Lib::PolyPath64& outer_node, std::vector<Polygon>& out) {
  Polygon polygon{.outline = clipper_path_to_contour(outer_node.Polygon()), .holes = {}};
  for (std::size_t i = 0; i < outer_node.Count(); ++i) {
    const Clipper2Lib::PolyPath64* hole = outer_node.Child(i);
    polygon.holes.push_back(clipper_path_to_contour(hole->Polygon()));
    for (std::size_t j = 0; j < hole->Count(); ++j) {
      collect_outer_polygon(*hole->Child(j), out);
    }
  }
  out.push_back(std::move(polygon));
}

// NOLINTNEXTLINE(misc-include-cleaner)
std::vector<Polygon> polytree_to_polygons(const Clipper2Lib::PolyTree64& root) {
  std::vector<Polygon> result;
  for (const auto& child : root) {
    collect_outer_polygon(*child, result);
  }
  return result;
}

// NOLINTNEXTLINE(misc-include-cleaner)
Clipper2Lib::ClipType to_clip_type(BooleanOp op) {
  switch (op) {
  case BooleanOp::Union:
    return Clipper2Lib::ClipType::Union;
  case BooleanOp::Intersect:
    return Clipper2Lib::ClipType::Intersection;
  case BooleanOp::Difference:
    return Clipper2Lib::ClipType::Difference;
  }
  return Clipper2Lib::ClipType::Union;
}

} // namespace

std::vector<Polygon>
boolean_op(BooleanOp op, const std::vector<Polygon>& subjects, const std::vector<Polygon>& clips) {
  const ClipperPaths subject_paths = polygons_to_clipper_paths(subjects);
  const ClipperPaths clip_paths = polygons_to_clipper_paths(clips);

  // NOLINTNEXTLINE(misc-include-cleaner)
  Clipper2Lib::Clipper64 clipper;
  clipper.AddSubject(subject_paths);
  if (!clip_paths.empty()) {
    clipper.AddClip(clip_paths);
  }

  Clipper2Lib::PolyTree64 solution;
  Clipper2Lib::Paths64 open_solution;
  // NOLINTNEXTLINE(misc-include-cleaner)
  clipper.Execute(to_clip_type(op), Clipper2Lib::FillRule::NonZero, solution, open_solution);

  return polytree_to_polygons(solution);
}

} // namespace pcbir::geometry
