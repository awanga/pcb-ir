// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_PAD_SHAPE_EXTENSION_HPP
#define PCBIR_KICAD_PAD_SHAPE_EXTENSION_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "pad_shape.hpp"

// The PCBIR_KICAD/pad_shape extension (docs/rfcs/0003-kicad-importer-
// exporter.md -- "Pad shape fidelity", docs/extensions-governance.md):
// carries a Pad's original KiCad-authored parametric shape (rect/circle/
// oval/roundrect/trapezoid, never "custom") losslessly alongside the
// geometric Polygon outline every reader gets regardless, so a reader that
// understands this extension reconstructs the exact original pad primitive
// (Preserved) while one that doesn't still has fully correct copper
// geometry from Pad::outline alone (Approximated -- the extension
// mechanism's own documented graceful-degradation contract). Internal to
// import_footprint.cpp/export_footprint.cpp, so this lives under src/,
// mirroring pad_shape.hpp's own precedent.
namespace pcbir::kicad {

inline constexpr const char* PAD_SHAPE_EXTENSION_NAMESPACE = "PCBIR_KICAD";
inline constexpr const char* PAD_SHAPE_EXTENSION_NAME = "pad_shape";
inline constexpr uint32_t PAD_SHAPE_EXTENSION_VERSION = 1;

// A decoded payload: `params` is the pad's original parametric shape
// (pad_shape.hpp); `rotation_e6` is the pad's own absolute rotation as
// imported (PCB-IR's counterclockwise-positive convention) -- geometry::Pad
// stores no rotation field of its own (only an already-rotated absolute
// outline), so this extension is this pad's only place that value survives
// for the exporter to recover the original `(pad ... (at X Y ANGLE) ...)`
// primitive from.
struct DecodedPadShape {
  PadShapeParams params;
  int64_t rotation_e6 = 0;
};

// Encodes `params`/`rotation_e6` into a PCBIR_KICAD/pad_shape v1 payload: a
// fixed 56-byte little-endian layout --
//   [0]      shape (KicadPadShape, one byte)
//   [1, 8)   reserved, always zero
//   [8, 16)  width_nm             (int64_t)
//   [16, 24) height_nm            (int64_t)
//   [24, 32) roundrect_radius_nm  (int64_t)
//   [32, 40) trapezoid_delta_x_nm (int64_t)
//   [40, 48) trapezoid_delta_y_nm (int64_t)
//   [48, 56) rotation_e6          (int64_t)
// -- little-endian and a fixed byte layout (rather than an in-memory struct
// copy) so the payload is portable across platforms regardless of this
// project's own build architecture, consistent with docs/format-spec.md's
// "no platform-dependent encoding in serialized output" discipline.
[[nodiscard]] std::vector<uint8_t> encode_pad_shape_extension(const PadShapeParams& params,
                                                              int64_t rotation_e6);

// Decodes a payload produced by encode_pad_shape_extension. Returns
// std::nullopt for a payload of the wrong size or an unrecognized shape
// byte -- a caller treats this exactly like "no extension present" (fall
// back to the Approximated custom-pad path) rather than throwing, since a
// payload from an unrecognized future version is precisely the "safely
// ignore what you don't understand" case the extension mechanism's own
// contract requires (docs/extensions-governance.md).
[[nodiscard]] std::optional<DecodedPadShape>
decode_pad_shape_extension(std::span<const uint8_t> payload);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_PAD_SHAPE_EXTENSION_HPP
