// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_ARC_HPP
#define PCBIR_GEOMETRY_ARC_HPP

#include "pcbir/geometry/point.hpp"

#include <cstdint>

namespace pcbir::geometry {

// Which of the two circular arcs (through `center`) connecting `start` to
// `end` this Arc represents -- the one remaining ambiguity once a center is
// fixed (see Arc's own comment).
enum class ArcDirection : uint8_t { Clockwise, CounterClockwise };

// A circular arc stored exactly as endpoints + center + sweep direction --
// never as a flattened point sequence, and never reduced to just start/end
// (that would lose the center and be a Segment). Splines/teardrops are the
// things that get flattened to arcs/segments on import; an arc round-trips
// as-is.
//
// Fixing `center` and the two endpoints leaves exactly one remaining
// ambiguity -- which of the two arcs between `start` and `end` this is --
// resolved by `direction`. `start == end` is a degenerate case meaning a
// full circle of radius |center - start|; this type does not reject it.
struct Arc {
  Point start;
  Point end;
  Point center;
  ArcDirection direction = ArcDirection::Clockwise;

  friend constexpr bool operator==(const Arc&, const Arc&) = default;

  // Exact (no floating point, no sqrt) check that `start` and `end` are
  // equidistant from `center`, i.e. that `center` is actually a valid
  // circle center for this arc. Returns false (not just "unequal") if a
  // distance-squared computation would overflow, since validity can't be
  // established in that case either.
  [[nodiscard]] bool is_valid() const {
    const Point to_start = start - center;
    const Point to_end = end - center;
    int64_t radius_sq_start = 0;
    int64_t radius_sq_end = 0;
    if (!dot(to_start, to_start, radius_sq_start) || !dot(to_end, to_end, radius_sq_end)) {
      return false;
    }
    return radius_sq_start == radius_sq_end;
  }
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_ARC_HPP
