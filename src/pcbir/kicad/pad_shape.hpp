// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_PAD_SHAPE_HPP
#define PCBIR_KICAD_PAD_SHAPE_HPP

#include "pcbir/geometry/polygon.hpp"

#include <cstdint>

// Converts KiCad's parametric pad shapes into PCB-IR's already-resolved
// Polygon outline (geometry/pad.hpp doesn't store a shape parameter --
// docs/rfcs/0003-kicad-importer-exporter.md's "Pad shape fidelity" gap).
// Not part of the public API (used only by import_footprint.cpp), so this
// lives under src/.
namespace pcbir::kicad {

enum class KicadPadShape : uint8_t { Rect, Circle, Oval, RoundRect, Trapezoid };

// All dimensions in nanometres, all shapes built pad-local (centered at the
// origin, unrotated) -- the caller (import_footprint.cpp) rotates and
// translates the result into absolute board coordinates using the pad's
// own resolved position/orientation, the same way import_geometry.cpp
// composes board-outline spans. `roundrect_radius_nm` is only meaningful
// for RoundRect; `trapezoid_delta_x_nm`/`trapezoid_delta_y_nm` only for
// Trapezoid (KiCad's UI allows setting only one axis at a time; if both are
// given, x takes precedence).
struct PadShapeParams {
  KicadPadShape shape = KicadPadShape::Rect;
  int64_t width_nm = 0;
  int64_t height_nm = 0;
  int64_t roundrect_radius_nm = 0;
  int64_t trapezoid_delta_x_nm = 0;
  int64_t trapezoid_delta_y_nm = 0;
};

// Builds the pad-local outline polygon for `params`. The result always
// satisfies geometry::validate(Polygon) (CounterClockwise outline, no
// holes) -- callers never need their own winding-canonicalization pass.
//
// Throws ImportError (pcbir/kicad/import.hpp) if `shape` is Circle and
// width_nm != height_nm (KiCad itself requires a circular pad's size to be
// square; a mismatch means malformed input, not a shape this function
// should silently approximate).
[[nodiscard]] geometry::Polygon build_pad_outline(const PadShapeParams& params);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_PAD_SHAPE_HPP
