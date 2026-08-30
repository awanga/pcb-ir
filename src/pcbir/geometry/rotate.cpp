// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/rotate.hpp"

#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/checked_arith.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <variant>

namespace pcbir::geometry {

namespace {

constexpr int64_t DEGREES_E6_PER_FULL_TURN = 360'000'000;
constexpr int64_t DEGREES_E6_PER_QUARTER_TURN = 90'000'000;

// Normalizes `angle_e6` into [0, 360_000_000) so a negative or
// more-than-one-full-turn input still routes through the same quarter-turn
// fast-path check below.
int64_t normalize_angle_e6(int64_t angle_e6) {
  int64_t normalized = angle_e6 % DEGREES_E6_PER_FULL_TURN;
  if (normalized < 0) {
    normalized += DEGREES_E6_PER_FULL_TURN;
  }
  return normalized;
}

int64_t negate(int64_t value) {
  int64_t result = 0;
  const bool ok = checked_sub(int64_t{0}, value, result);
  assert(ok && "negation overflowed int64_t nanometers");
  (void)ok;
  return result;
}

// Exact rotation of `relative` (already translated so the rotation origin
// is at the coordinate-space origin) by `quarter_turns` counterclockwise
// 90-degree steps (0-3) -- integer swap/negate only, bit-identical across
// platforms.
Point rotate_quarter_turns(const Point& relative, int64_t quarter_turns) {
  switch (quarter_turns) {
  case 1:
    return Point{.x = negate(relative.y), .y = relative.x};
  case 2:
    return Point{.x = negate(relative.x), .y = negate(relative.y)};
  case 3:
    return Point{.x = relative.y, .y = negate(relative.x)};
  default:
    return relative;
  }
}

Point rotate_relative(const Point& relative, int64_t angle_e6) {
  const int64_t normalized = normalize_angle_e6(angle_e6);
  if (normalized % DEGREES_E6_PER_QUARTER_TURN == 0) {
    return rotate_quarter_turns(relative, normalized / DEGREES_E6_PER_QUARTER_TURN);
  }

  // Non-90-degree-multiple rotation: placing a point at an arbitrary angle
  // is inherently irrational in integer coordinates in general (see
  // rotate.hpp), so this follows the same float-trig-plus-llround approach
  // already used at the Clipper2 arc-flattening boundary (boolean.cpp).
  constexpr double pi = std::numbers::pi;
  const double radians = (static_cast<double>(normalized) / 1'000'000.0) * (pi / 180.0);
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  const auto x = static_cast<double>(relative.x);
  const auto y = static_cast<double>(relative.y);
  return Point{
      .x = static_cast<int64_t>(std::llround((x * cosine) - (y * sine))),
      .y = static_cast<int64_t>(std::llround((x * sine) + (y * cosine))),
  };
}

} // namespace

Point rotate(const Point& point, const Point& origin, int64_t angle_e6) {
  const Point relative = point - origin;
  return origin + rotate_relative(relative, angle_e6);
}

Segment rotate(const Segment& segment, const Point& origin, int64_t angle_e6) {
  return Segment{.start = rotate(segment.start, origin, angle_e6),
                 .end = rotate(segment.end, origin, angle_e6)};
}

Arc rotate(const Arc& arc, const Point& origin, int64_t angle_e6) {
  return Arc{.start = rotate(arc.start, origin, angle_e6),
             .end = rotate(arc.end, origin, angle_e6),
             .center = rotate(arc.center, origin, angle_e6),
             .direction = arc.direction};
}

Span rotate(const Span& span, const Point& origin, int64_t angle_e6) {
  if (const auto* segment = std::get_if<Segment>(&span)) {
    return Span{rotate(*segment, origin, angle_e6)};
  }
  return Span{rotate(std::get<Arc>(span), origin, angle_e6)};
}

Contour rotate(const Contour& contour, const Point& origin, int64_t angle_e6) {
  Contour result;
  result.spans.reserve(contour.spans.size());
  for (const Span& span : contour.spans) {
    result.spans.push_back(rotate(span, origin, angle_e6));
  }
  return result;
}

Polygon rotate(const Polygon& polygon, const Point& origin, int64_t angle_e6) {
  Polygon result;
  result.outline = rotate(polygon.outline, origin, angle_e6);
  result.holes.reserve(polygon.holes.size());
  for (const Contour& hole : polygon.holes) {
    result.holes.push_back(rotate(hole, origin, angle_e6));
  }
  return result;
}

} // namespace pcbir::geometry
