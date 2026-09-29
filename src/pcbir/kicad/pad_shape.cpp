// SPDX-License-Identifier: Apache-2.0
#include "pad_shape.hpp"

#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"
#include "pcbir/kicad/import.hpp"

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

#include "coordinate_util.hpp"

namespace pcbir::kicad {

namespace {

using geometry::Arc;
using geometry::ArcDirection;
using geometry::Contour;
using geometry::Point;
using geometry::Polygon;
using geometry::Segment;
using geometry::Span;

[[nodiscard]] Polygon finish(std::vector<Span> spans) {
  return Polygon{.outline = canonicalize_winding(Contour{.spans = std::move(spans)}), .holes = {}};
}

// An axis-aligned rectangle of `width_nm` x `height_nm` centered at the
// origin. Also exactly the shape a Trapezoid with zero delta reduces to
// (build_trapezoid, below), and the shape a RoundRect with zero radius
// reduces to. Both parameters are the same unit and it's the caller's job
// to keep them in the right order; a distinct wrapper type per dimension
// would be overkill for this small, internal shape-construction type.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] Polygon build_rect(int64_t width_nm, int64_t height_nm) {
  const int64_t hw = width_nm / 2;
  const int64_t hh = height_nm / 2;
  return finish({
      Span{Segment{.start = Point{.x = hw, .y = -hh}, .end = Point{.x = hw, .y = hh}}},
      Span{Segment{.start = Point{.x = hw, .y = hh}, .end = Point{.x = -hw, .y = hh}}},
      Span{Segment{.start = Point{.x = -hw, .y = hh}, .end = Point{.x = -hw, .y = -hh}}},
      Span{Segment{.start = Point{.x = -hw, .y = -hh}, .end = Point{.x = hw, .y = -hh}}},
  });
}

// A circle of diameter `diameter_nm` centered at the origin, represented
// as 4 true quarter-circle Arc spans (never flattened -- geometry/arc.hpp)
// through the 4 cardinal points, verified against real pcbnew-computed pad
// polygons (docs/rfcs/0003-kicad-importer-exporter.md). Each arc travels
// its *minor* (90-degree) sweep counterclockwise from start to end (e.g.
// east=(r,0) to north=(0,r) via the first-quadrant point at 45 degrees,
// not the 270-degree major arc the other way around) -- the same
// "CounterClockwise means increasing atan2 angle from start to end"
// convention kicad_three_point_arc_to_pcbir (coordinate_util.hpp) already
// uses and is verified against real pcbnew arcs for.
[[nodiscard]] Polygon build_circle(int64_t diameter_nm) {
  const int64_t r = diameter_nm / 2;
  const Point center{.x = 0, .y = 0};
  const Point east{.x = r, .y = 0};
  const Point north{.x = 0, .y = r};
  const Point west{.x = -r, .y = 0};
  const Point south{.x = 0, .y = -r};
  return finish({
      Span{Arc{.start = east,
               .end = north,
               .center = center,
               .direction = ArcDirection::CounterClockwise}},
      Span{Arc{.start = north,
               .end = west,
               .center = center,
               .direction = ArcDirection::CounterClockwise}},
      Span{Arc{.start = west,
               .end = south,
               .center = center,
               .direction = ArcDirection::CounterClockwise}},
      Span{Arc{.start = south,
               .end = east,
               .center = center,
               .direction = ArcDirection::CounterClockwise}},
  });
}

// A "stadium" shape: two straight edges joined by two semicircular end
// caps -- KiCad's `oval` pad shape. Degenerates to build_circle when
// width_nm == height_nm.
[[nodiscard]] Polygon build_oval(int64_t width_nm, int64_t height_nm) {
  if (width_nm == height_nm) {
    return build_circle(width_nm);
  }

  const bool wide = width_nm > height_nm;
  const int64_t minor = wide ? height_nm : width_nm;
  const int64_t major = wide ? width_nm : height_nm;
  const int64_t r = minor / 2;
  const int64_t half_flat = (major - minor) / 2;

  // Built wide (major axis along X); swap X/Y at the end for the tall case.
  const Point cap_a{.x = half_flat, .y = 0};
  const Point cap_b{.x = -half_flat, .y = 0};
  std::vector<Span> spans{
      Span{
          Segment{.start = Point{.x = half_flat, .y = -r}, .end = Point{.x = -half_flat, .y = -r}}},
      Span{Arc{.start = Point{.x = -half_flat, .y = -r},
               .end = Point{.x = -half_flat, .y = r},
               .center = cap_b,
               .direction = ArcDirection::Clockwise}},
      Span{Segment{.start = Point{.x = -half_flat, .y = r}, .end = Point{.x = half_flat, .y = r}}},
      Span{Arc{.start = Point{.x = half_flat, .y = r},
               .end = Point{.x = half_flat, .y = -r},
               .center = cap_a,
               .direction = ArcDirection::Clockwise}},
  };
  if (!wide) {
    for (Span& span : spans) {
      const auto swap_xy = [](Point& p) { p = Point{.x = p.y, .y = p.x}; };
      if (auto* segment = std::get_if<Segment>(&span)) {
        swap_xy(segment->start);
        swap_xy(segment->end);
      } else {
        auto& arc = std::get<Arc>(span);
        swap_xy(arc.start);
        swap_xy(arc.end);
        swap_xy(arc.center);
      }
    }
  }
  return finish(std::move(spans));
}

// An axis-aligned rectangle with all 4 corners rounded to `radius_nm`,
// built as 4 straight edges + 4 true quarter-circle Arc corners (the same
// construction geometry::rotate's non-90-degree fallback and the
// rounded-rect-outline golden-corpus board both already exercise) --
// KiCad's `roundrect` pad shape. Degenerates to build_rect when
// radius_nm <= 0 (a zero-length arc span would otherwise result).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] Polygon build_roundrect(int64_t width_nm, int64_t height_nm, int64_t radius_nm) {
  if (radius_nm <= 0) {
    return build_rect(width_nm, height_nm);
  }

  const int64_t hw = width_nm / 2;
  const int64_t hh = height_nm / 2;
  const int64_t a = hw - radius_nm;
  const int64_t b = hh - radius_nm;

  const Point p_bottom_right{.x = a, .y = -hh};
  const Point p_bottom_left{.x = -a, .y = -hh};
  const Point p_left_bottom{.x = -hw, .y = -b};
  const Point p_left_top{.x = -hw, .y = b};
  const Point p_top_left{.x = -a, .y = hh};
  const Point p_top_right{.x = a, .y = hh};
  const Point p_right_top{.x = hw, .y = b};
  const Point p_right_bottom{.x = hw, .y = -b};

  return finish({
      Span{Segment{.start = p_bottom_right, .end = p_bottom_left}},
      Span{Arc{.start = p_bottom_left,
               .end = p_left_bottom,
               .center = Point{.x = -a, .y = -b},
               .direction = ArcDirection::Clockwise}},
      Span{Segment{.start = p_left_bottom, .end = p_left_top}},
      Span{Arc{.start = p_left_top,
               .end = p_top_left,
               .center = Point{.x = -a, .y = b},
               .direction = ArcDirection::Clockwise}},
      Span{Segment{.start = p_top_left, .end = p_top_right}},
      Span{Arc{.start = p_top_right,
               .end = p_right_top,
               .center = Point{.x = a, .y = b},
               .direction = ArcDirection::Clockwise}},
      Span{Segment{.start = p_right_top, .end = p_right_bottom}},
      Span{Arc{.start = p_right_bottom,
               .end = p_bottom_right,
               .center = Point{.x = a, .y = -b},
               .direction = ArcDirection::Clockwise}},
  });
}

// A quadrilateral formed by shearing one pair of a rectangle's opposite
// edges by `delta_x_nm`/`delta_y_nm` -- KiCad's `trapezoid` pad shape.
// Verified against a real pcbnew-computed pad polygon
// (docs/rfcs/0003-kicad-importer-exporter.md): exactly one of
// delta_x_nm/delta_y_nm is expected to be nonzero (KiCad's own UI only
// ever sets one axis at a time); if both are, delta_x_nm takes precedence.
// Reduces to build_rect when both are zero.
[[nodiscard]] Polygon
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
build_trapezoid(int64_t width_nm, int64_t height_nm, int64_t delta_x_nm, int64_t delta_y_nm) {
  const int64_t hw = width_nm / 2;
  const int64_t hh = height_nm / 2;

  if (delta_x_nm != 0) {
    const int64_t right_half_height = hh - (delta_x_nm / 2);
    const int64_t left_half_height = hh + (delta_x_nm / 2);
    return finish({
        Span{Segment{.start = Point{.x = hw, .y = -right_half_height},
                     .end = Point{.x = hw, .y = right_half_height}}},
        Span{Segment{.start = Point{.x = hw, .y = right_half_height},
                     .end = Point{.x = -hw, .y = left_half_height}}},
        Span{Segment{.start = Point{.x = -hw, .y = left_half_height},
                     .end = Point{.x = -hw, .y = -left_half_height}}},
        Span{Segment{.start = Point{.x = -hw, .y = -left_half_height},
                     .end = Point{.x = hw, .y = -right_half_height}}},
    });
  }

  const int64_t top_half_width = hw + (delta_y_nm / 2);
  const int64_t bottom_half_width = hw - (delta_y_nm / 2);
  return finish({
      Span{Segment{.start = Point{.x = bottom_half_width, .y = -hh},
                   .end = Point{.x = -bottom_half_width, .y = -hh}}},
      Span{Segment{.start = Point{.x = -bottom_half_width, .y = -hh},
                   .end = Point{.x = -top_half_width, .y = hh}}},
      Span{Segment{.start = Point{.x = -top_half_width, .y = hh},
                   .end = Point{.x = top_half_width, .y = hh}}},
      Span{Segment{.start = Point{.x = top_half_width, .y = hh},
                   .end = Point{.x = bottom_half_width, .y = -hh}}},
  });
}

} // namespace

const char* kicad_pad_shape_keyword(KicadPadShape shape) {
  switch (shape) {
  case KicadPadShape::Rect:
    return "rect";
  case KicadPadShape::Circle:
    return "circle";
  case KicadPadShape::Oval:
    return "oval";
  case KicadPadShape::RoundRect:
    return "roundrect";
  case KicadPadShape::Trapezoid:
    return "trapezoid";
  }
  throw ImportError("unrecognized KiCad pad shape");
}

Polygon build_pad_outline(const PadShapeParams& params) {
  switch (params.shape) {
  case KicadPadShape::Rect:
    return build_rect(params.width_nm, params.height_nm);
  case KicadPadShape::Circle:
    if (params.width_nm != params.height_nm) {
      throw ImportError("circular pad must have equal width and height");
    }
    return build_circle(params.width_nm);
  case KicadPadShape::Oval:
    return build_oval(params.width_nm, params.height_nm);
  case KicadPadShape::RoundRect:
    return build_roundrect(params.width_nm, params.height_nm, params.roundrect_radius_nm);
  case KicadPadShape::Trapezoid:
    return build_trapezoid(params.width_nm,
                           params.height_nm,
                           params.trapezoid_delta_x_nm,
                           params.trapezoid_delta_y_nm);
  }
  throw ImportError("unrecognized KiCad pad shape");
}

} // namespace pcbir::kicad
