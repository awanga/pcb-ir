// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_BBOX_HPP
#define PCBIR_GEOMETRY_BBOX_HPP

#include "pcbir/geometry/point.hpp"

#include <algorithm>

namespace pcbir::geometry {

// An axis-aligned bounding box, inclusive of both corners. A default-
// constructed BBox is *empty* (min > max on both axes), not the zero-area
// box at the origin -- see is_empty().
struct BBox {
  Point min{.x = 1, .y = 1};
  Point max{.x = 0, .y = 0};

  [[nodiscard]] bool is_empty() const { return min.x > max.x || min.y > max.y; }

  [[nodiscard]] bool contains(const Point& p) const {
    return !is_empty() && p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
  }

  [[nodiscard]] bool intersects(const BBox& other) const {
    if (is_empty() || other.is_empty()) {
      return false;
    }
    return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y &&
           max.y >= other.min.y;
  }

  // Smallest BBox containing both `*this` and `other` (or just the
  // non-empty one, if either is empty).
  [[nodiscard]] BBox union_with(const BBox& other) const {
    if (is_empty()) {
      return other;
    }
    if (other.is_empty()) {
      return *this;
    }
    return BBox{
        .min = Point{.x = std::min(min.x, other.min.x), .y = std::min(min.y, other.min.y)},
        .max = Point{.x = std::max(max.x, other.max.x), .y = std::max(max.y, other.max.y)},
    };
  }

  [[nodiscard]] static BBox from_point(const Point& p) { return BBox{.min = p, .max = p}; }
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_BBOX_HPP
