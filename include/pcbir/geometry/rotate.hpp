// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_ROTATE_HPP
#define PCBIR_GEOMETRY_ROTATE_HPP

#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/segment.hpp"

#include <cstdint>

namespace pcbir::geometry {

// Rotates `point` counterclockwise by `angle_e6` (degrees * 1e6, matching
// Footprint::rotation_e6) about `origin`. Not KiCad-specific: any importer
// placing footprint-relative geometry at absolute board coordinates needs
// this (docs/rfcs/0002-kicad-schema-foundation.md).
//
// Exact (no floating point, so bit-identical across platforms) for angles
// that are a multiple of 90 degrees -- the overwhelming majority of real
// footprints -- via integer swap/negate. For any other angle, placing a
// point at an arbitrary angle is inherently irrational in integer
// coordinates in general, so this falls back to double-precision
// cos/sin (the same std::atan2/cos/sin/hypot approach already used at the
// Clipper2 arc-flattening boundary, boolean.hpp): a documented best-effort
// approximation, rounded to the nearest nanometer half-away-from-zero, not
// guaranteed bit-exact across platforms/compilers the way the 90-degree
// fast path is.
[[nodiscard]] Point rotate(const Point& point, const Point& origin, int64_t angle_e6);

[[nodiscard]] Segment rotate(const Segment& segment, const Point& origin, int64_t angle_e6);
[[nodiscard]] Arc rotate(const Arc& arc, const Point& origin, int64_t angle_e6);
[[nodiscard]] Span rotate(const Span& span, const Point& origin, int64_t angle_e6);
[[nodiscard]] Contour rotate(const Contour& contour, const Point& origin, int64_t angle_e6);
[[nodiscard]] Polygon rotate(const Polygon& polygon, const Point& origin, int64_t angle_e6);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_ROTATE_HPP
