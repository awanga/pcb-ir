// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_IMPORT_FOOTPRINT_HPP
#define PCBIR_KICAD_IMPORT_FOOTPRINT_HPP

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

namespace pcbir::kicad {

// The two-layer output of import_footprints: geometry (Footprint/Pad/Via)
// and connectivity (the input `nets` snapshot's Net table, carried
// forward unchanged, plus one new Pin per Pad/Via created here).
// Returned together, rather than through an in/out workspace parameter,
// so this stays a plain, independently-testable function in the same
// style as import_stackup/import_board_outline.
struct FootprintImportResult {
  geometry::GeometrySnapshot geometry;
  connectivity::ConnectivitySnapshot connectivity;
};

// Imports every `(footprint ...)` in a parsed `.kicad_pcb` root
// expression (see parse_sexpr) into Footprint entities, and each of their
// child `(pad ...)` entries into either a Pad (smd/connect types) or a
// Via (thru_hole/np_thru_hole types, modeled as a plated-through-hole pad
// -- geometry/via.hpp). `stackup` must already contain the board's Copper
// Layers and LayerStack (import_stackup's output); `nets` must already
// contain every Net a pad might reference by name (import_nets's output)
// -- both are read-only inputs, never mutated.
//
// A pad's absolute position and rotation are computed directly from its
// own `(at x y [angle])` and its owning footprint's `(at x y [angle])`:
// verified against real pcbnew output that a pad's stored angle is
// already its full absolute rotation (never needs its footprint's angle
// added), while a pad's *position* is footprint-relative and does need
// the footprint's own rotation applied
// (docs/rfcs/0003-kicad-importer-exporter.md's mirror/rotate composition
// rule -- load-bearing for the exporter, not needed here since PCB-IR's
// Pad/Via already store final resolved absolute geometry). A pad's
// parametric shape (rect/circle/oval/roundrect/trapezoid) is converted to
// a concrete outline via build_pad_outline (pad_shape.hpp). A `custom`
// pad is supported only in the single-`(gr_poly (pts ...))`-primitive
// form the exporter itself emits (export_footprint.cpp, since
// geometry::Pad has no shape-parameter field to export a parametric
// shape faithfully from -- RFC 0003's own documented "Pad shape
// fidelity" gap); any other primitive combination (multiple primitives,
// or a primitive kind other than gr_poly) is Unsupported and throws
// ImportError rather than being silently approximated.
//
// Throws ImportError (pcbir/kicad/import.hpp) on a malformed footprint/pad
// (missing required field, unrecognized pad type/shape, a layer/net
// reference that doesn't resolve against `stackup`/`nets`).
[[nodiscard]] FootprintImportResult
import_footprints(const SExpr& kicad_pcb,
                  const stackup::StackupSnapshot& stackup,
                  const connectivity::ConnectivitySnapshot& nets);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_FOOTPRINT_HPP
