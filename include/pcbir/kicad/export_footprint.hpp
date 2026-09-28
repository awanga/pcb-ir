// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_EXPORT_FOOTPRINT_HPP
#define PCBIR_KICAD_EXPORT_FOOTPRINT_HPP

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <vector>

namespace pcbir::kicad {

// Exports every Footprint in `geometry` into one `(footprint ...)` SExpr
// node, each containing its own `(pad ...)` children -- the exporter's
// inverse of import_footprints (pcbir/kicad/import_footprint.hpp).
//
// Position/rotation: recovered via footprint_local_point
// (coordinate_util.hpp), the exact mathematical inverse of
// resolve_absolute_placement's forward formula, side-agnostic like that
// formula itself -- PCB-IR's Pad/Via already store final resolved
// absolute geometry, so (unlike the RFC's original draft language) no
// separate un-mirroring step is needed for position; see coordinate_util.
// hpp's own comment on footprint_local_point for the full reasoning.
//
// Pad shape: every Pad is exported as a KiCad `custom`-shape pad (never
// the original rect/circle/oval/roundrect/trapezoid primitive) --
// geometry::Pad stores only an already-resolved outline Polygon, with no
// shape-parameter field to recover the original primitive from (RFC
// 0003's own documented "Pad shape fidelity" gap; the proposed
// PCBIR_KICAD/pad_shape extension that would close it isn't implemented
// yet). The custom pad's `(primitives (gr_poly (pts ...)))` is built
// directly from the pad's absolute outline offset by its own absolute
// center, needing no footprint-rotation inversion at all (verified
// against real pcbnew output: a custom pad's own `(at x y)` angle field
// controls primitive rotation independently of its shape, so emitting
// angle 0 and already-placed-relative-to-center primitive points
// reproduces the exact original absolute shape once reloaded). An Arc
// span in a pad's outline (circle/oval/roundrect pads all produce these)
// is flattened to chords (coordinate_util.hpp's flatten_arc) since
// KiCad's `gr_poly` primitive supports only straight edges -- the same
// documented best-effort approximation geometry::boolean_op already uses
// at the Clipper2 boundary. This makes pad shape **Approximated**, not
// Preserved, on this pass: geometrically correct copper, but reported as
// a generic custom shape rather than "this was authored as a rect".
//
// Pad/Via net: resolved via each entity's connectivity::Pin (neither
// geometry::Pad nor geometry::Via stores a net field directly).
//
// A footprint's own KiCad library identifier (the `"LIB:NAME"` string
// immediately after `footprint`) is not stored in geometry::Footprint at
// all (a pre-existing schema gap from RFC 0002, not introduced here) --
// a placeholder of the form `"PCBIR:<reference designator>"` is emitted
// instead, always loadable but never round-tripping the original library
// reference (Lost, not Approximated: there is no original value to
// approximate towards).
//
// Throws ExportError if a Pad/Via's `layer`/`start_layer`/`end_layer`
// doesn't resolve against `stackup`, or its net doesn't resolve against
// `nets`.
[[nodiscard]] std::vector<SExpr> export_footprints(const geometry::GeometrySnapshot& geometry,
                                                   const stackup::StackupSnapshot& stackup,
                                                   const connectivity::ConnectivitySnapshot& nets);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_EXPORT_FOOTPRINT_HPP
