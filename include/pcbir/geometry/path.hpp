// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_PATH_HPP
#define PCBIR_GEOMETRY_PATH_HPP

#include "pcbir/geometry/contour.hpp"

#include <cstdint>
#include <vector>

namespace pcbir::geometry {

// One span of a Path, carrying its own width -- the "per-span width"
// requirement for track/trace and silkscreen-stroke entities
// (docs/architecture.md).
struct WidthSpan {
  Span geometry;
  int64_t width_nm = 0;

  friend bool operator==(const WidthSpan&, const WidthSpan&) = default;
};

// An ordered, connected sequence of width spans -- unlike Contour, a Path
// is *not* required to close (the last span's end need not meet the
// first span's start), matching an open trace/stroke running between two
// points.
struct Path {
  std::vector<WidthSpan> spans;
};

enum class PathValidity : uint8_t {
  Valid,
  Empty,
  Discontinuous,
  InvalidArcSpan,
  NonPositiveWidth,
};

// Validates per-span continuity (consecutive spans share an endpoint),
// per-arc-span exactness (Arc::is_valid), and that every span has a
// positive width. Does not require closure (see Path's own comment).
[[nodiscard]] PathValidity validate(const Path& path);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_PATH_HPP
