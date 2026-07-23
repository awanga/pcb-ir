// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_BEZIER_HPP
#define PCBIR_GEOMETRY_BEZIER_HPP

#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/segment.hpp"

#include <vector>

namespace pcbir::geometry {

// A cubic Bezier curve -- the standard curve primitive splines and teardrop
// fillets are expressed in by the board-file formats this project imports
// from. Always flattened to segments before entering the IR (see
// flatten_cubic_bezier); PCB-IR's own model has no curved-spline entity.
struct CubicBezier {
  Point p0;
  Point p1;
  Point p2;
  Point p3;
};

// Fixed recursion depth for flatten_cubic_bezier(): every curve is
// bisected exactly this many times, regardless of how flat or sharp it is.
// A cubic Bezier's control-polygon deviation from the true curve shrinks by
// (at least) 4x per bisection, so depth n bounds the worst-case flattening
// error at (initial deviation)/4^n. At depth 10 (1024 segments per curve),
// a curve whose control points span the full documented coordinate range
// (~9.2e9 m, see docs/format-spec.md) still flattens to within ~1
// micrometer of the true curve; any real board-scale curve (mm-cm control
// polygons) flattens far tighter than that. See docs/format-spec.md --
// Geometry encoding for the full derivation.
inline constexpr int BEZIER_FLATTEN_DEPTH = 10;

// Deterministically flattens `curve` to a connected polyline of segments
// via BEZIER_FLATTEN_DEPTH De Casteljau bisections. Always produces exactly
// 2^BEZIER_FLATTEN_DEPTH segments, with the first segment's start == p0 and
// the last segment's end == p3. Uses only overflow-checked integer
// midpoints (add + divide-by-2) -- no floating point, no transcendental
// functions, and no adaptive threshold comparison -- so the result is
// bit-for-bit identical for the same input on every supported platform.
[[nodiscard]] std::vector<Segment> flatten_cubic_bezier(const CubicBezier& curve);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_BEZIER_HPP
