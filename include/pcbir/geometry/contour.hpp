// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_CONTOUR_HPP
#define PCBIR_GEOMETRY_CONTOUR_HPP

#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cstdint>
#include <variant>
#include <vector>

namespace pcbir::geometry {

// One edge of a Contour: either a straight Segment or a circular Arc -- the
// two members of the canonical primitive set (docs/architecture.md).
using Span = std::variant<Segment, Arc>;

[[nodiscard]] Point span_start(const Span& span);
[[nodiscard]] Point span_end(const Span& span);

// A closed loop of spans: consecutive spans share an endpoint (span[i]'s
// end == span[i+1]'s start), and the last span's end meets the first
// span's start.
struct Contour {
  std::vector<Span> spans;
};

enum class Orientation : uint8_t { Clockwise, CounterClockwise, Degenerate };

enum class ContourValidity : uint8_t {
  Valid,
  TooFewSpans,
  Discontinuous,
  InvalidArcSpan,
  SelfIntersecting,
};

// Signed-area-based orientation, computed over each span's *chord* (an
// arc's start-end straight line, not its true curved shape). Exact
// arc-arc intersection existence testing can require irrational
// coordinates in general (two circles generically meet at irrational
// points) -- unlike segment-segment crossing, which is decidable exactly
// via orientation predicates alone. Chord-based orientation/intersection
// is consistent with how Clipper2 already flattens arcs at the boolean-op
// boundary (docs/architecture.md); see docs/format-spec.md for the full
// rationale. Returns Degenerate on zero (or overflowing) signed area.
[[nodiscard]] Orientation orientation(const Contour& contour);

// Validates closure, per-span continuity, per-arc-span validity
// (Arc::is_valid), and chord-based self-intersection: no two
// *non-adjacent* spans' chords properly cross. Touching at a shared
// vertex (adjacent spans, by construction) is not itself a violation.
[[nodiscard]] ContourValidity validate(const Contour& contour);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_CONTOUR_HPP
