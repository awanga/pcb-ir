// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_BOOLEAN_HPP
#define PCBIR_GEOMETRY_BOOLEAN_HPP

#include "pcbir/geometry/polygon.hpp"

#include <cstdint>
#include <vector>

namespace pcbir::geometry {

enum class BooleanOp : uint8_t { Union, Intersect, Difference };

// Number of straight segments a full 360-degree arc is flattened into at
// the Clipper2 boundary (docs/format-spec.md); a partial arc gets a
// proportional share, rounded to at least one segment.
inline constexpr int ARC_FLATTEN_SEGMENTS_PER_FULL_TURN = 64;

// Runs a boolean operation over `subjects` and `clips` using Clipper2
// (the only place in this module Clipper2 is used -- kept out of every
// public header per the header-hygiene invariant, docs/architecture.md).
// `clips` is ignored for Union when empty (a plain multi-subject union).
//
// Arc spans are flattened to chords before crossing into Clipper2, which
// has no concept of a curved edge; every returned Polygon's outline and
// holes are therefore pure-Segment Contours, even if an input polygon had
// arcs. Unlike the deterministic (integer-only) Bezier flattening
// elsewhere in this module, arc flattening here uses floating point
// (placing a point on a circle is inherently irrational in integer
// coordinates in general) -- its output is a documented best-effort
// approximation, not a bit-exact one, and it is never the canonical
// serialized representation of the source arc.
//
// Input polygons are converted to Clipper2 paths in the order given
// (subjects then clips, each polygon's outline then its holes in
// declaration order, each contour's spans in declaration order) and never
// reordered by sorting, hashing, or pointer identity, so the same logical
// input always produces the same result.
[[nodiscard]] std::vector<Polygon>
boolean_op(BooleanOp op, const std::vector<Polygon>& subjects, const std::vector<Polygon>& clips);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_BOOLEAN_HPP
