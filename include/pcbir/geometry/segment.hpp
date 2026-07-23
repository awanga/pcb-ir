// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_SEGMENT_HPP
#define PCBIR_GEOMETRY_SEGMENT_HPP

#include "pcbir/geometry/point.hpp"

namespace pcbir::geometry {

// A straight line from `start` to `end`, stored exactly as its two
// endpoints -- no derived/cached fields, so equality is just endpoint
// equality and round-tripping it is trivially exact.
struct Segment {
  Point start;
  Point end;

  friend constexpr bool operator==(const Segment&, const Segment&) = default;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_SEGMENT_HPP
