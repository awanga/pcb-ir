// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/contour.hpp"

#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/checked_arith.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace pcbir::geometry {

Point span_start(const Span& span) {
  if (const auto* segment = std::get_if<Segment>(&span)) {
    return segment->start;
  }
  return std::get<Arc>(span).start;
}

Point span_end(const Span& span) {
  if (const auto* segment = std::get_if<Segment>(&span)) {
    return segment->end;
  }
  return std::get<Arc>(span).end;
}

namespace {

// Sign of cross(b - a, c - a): which side of the line a->b point c falls
// on. Returns false (indeterminate) rather than a sign on overflow.
[[nodiscard]] bool orientation_sign(const Point& a, const Point& b, const Point& c, int& sign_out) {
  int64_t value = 0;
  if (!cross(b - a, c - a, value)) {
    return false;
  }
  if (value > 0) {
    sign_out = 1;
  } else if (value < 0) {
    sign_out = -1;
  } else {
    sign_out = 0;
  }
  return true;
}

// `count` is the total span count a closed loop wraps around at; it plays
// a distinct role from the two span indices being compared.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] bool is_adjacent(std::size_t i, std::size_t j, std::size_t count) {
  const std::size_t distance = (i < j) ? (j - i) : (i - j);
  return distance == 1 || distance == count - 1;
}

} // namespace

bool chords_properly_cross(const Point& a1, const Point& a2, const Point& b1, const Point& b2) {
  int d1 = 0;
  int d2 = 0;
  int d3 = 0;
  int d4 = 0;
  if (!orientation_sign(a1, a2, b1, d1) || !orientation_sign(a1, a2, b2, d2) ||
      !orientation_sign(b1, b2, a1, d3) || !orientation_sign(b1, b2, a2, d4)) {
    return true;
  }
  return (d1 * d2 < 0) && (d3 * d4 < 0);
}

bool contour_strictly_contains(const Contour& contour, const Point& point) {
  const std::size_t count = contour.spans.size();
  bool inside = false;
  for (std::size_t i = 0; i < count; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Point p1 = span_start(contour.spans[i]);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Point p2 = span_start(contour.spans[(i + 1) % count]);
    if ((p1.y > point.y) == (p2.y > point.y)) {
      continue;
    }
    int64_t cross_val = 0;
    if (!cross(p2 - p1, point - p1, cross_val)) {
      return false;
    }
    const bool edge_rises = p2.y > p1.y; // p1.y != p2.y, guaranteed by the straddle test above
    if ((edge_rises && cross_val > 0) || (!edge_rises && cross_val < 0)) {
      inside = !inside;
    }
  }
  return inside;
}

Orientation orientation(const Contour& contour) {
  const std::size_t count = contour.spans.size();
  if (count == 0) {
    return Orientation::Degenerate;
  }

  int64_t signed_area_x2 = 0;
  for (std::size_t i = 0; i < count; ++i) {
    // `i` runs over [0, count), so both indices below are in bounds.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Point current = span_start(contour.spans[i]);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Point next = span_start(contour.spans[(i + 1) % count]);
    int64_t term = 0;
    int64_t updated = 0;
    if (!cross(current, next, term) || !checked_add(signed_area_x2, term, updated)) {
      return Orientation::Degenerate;
    }
    signed_area_x2 = updated;
  }

  if (signed_area_x2 > 0) {
    return Orientation::CounterClockwise;
  }
  if (signed_area_x2 < 0) {
    return Orientation::Clockwise;
  }
  return Orientation::Degenerate;
}

ContourValidity validate(const Contour& contour) {
  const std::size_t count = contour.spans.size();
  if (count < 3) {
    return ContourValidity::TooFewSpans;
  }

  for (std::size_t i = 0; i < count; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Span& span = contour.spans[i];
    if (const auto* arc = std::get_if<Arc>(&span)) {
      if (!arc->is_valid()) {
        return ContourValidity::InvalidArcSpan;
      }
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Span& next_span = contour.spans[(i + 1) % count];
    if (!(span_end(span) == span_start(next_span))) {
      return ContourValidity::Discontinuous;
    }
  }

  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      if (is_adjacent(i, j, count)) {
        continue;
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      const Span& a = contour.spans[i];
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      const Span& b = contour.spans[j];
      if (chords_properly_cross(span_start(a), span_end(a), span_start(b), span_end(b))) {
        return ContourValidity::SelfIntersecting;
      }
    }
  }

  return ContourValidity::Valid;
}

} // namespace pcbir::geometry
