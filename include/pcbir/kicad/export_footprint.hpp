// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_EXPORT_FOOTPRINT_HPP
#define PCBIR_KICAD_EXPORT_FOOTPRINT_HPP

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/extension.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <span>
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
// Pad shape: a Pad whose id has a matching PCBIR_KICAD/pad_shape entry in
// `extensions` (src/pcbir/kicad/pad_shape_extension.hpp; produced by
// import_footprints for every non-custom-shape pad) is exported as the
// real original rect/circle/oval/roundrect/trapezoid primitive, recovered
// exactly from the extension's payload -- **Preserved**. Every other Pad
// (no extension present, e.g. it was originally a `custom` shape, or the
// caller passed no extensions at all) falls back to a KiCad `custom`-
// shape pad instead -- geometry::Pad stores only an already-resolved
// outline Polygon, with no shape-parameter field of its own to recover a
// parametric primitive from (RFC 0003's own documented "Pad shape
// fidelity" gap). The custom pad's `(primitives (gr_poly (pts ...)))` is
// built directly from the pad's absolute outline offset by its own
// absolute center, needing no footprint-rotation inversion at all
// (verified against real pcbnew output: a custom pad's own `(at x y)`
// angle field controls primitive rotation independently of its shape, so
// emitting angle 0 and already-placed-relative-to-center primitive points
// reproduces the exact original absolute shape once reloaded). An Arc
// span in a pad's outline (circle/oval/roundrect pads all produce these)
// is flattened to chords (coordinate_util.hpp's flatten_arc) since
// KiCad's `gr_poly` primitive supports only straight edges -- the same
// documented best-effort approximation geometry::boolean_op already uses
// at the Clipper2 boundary. This fallback path makes pad shape
// **Approximated**, not Preserved: geometrically correct copper, but
// reported as a generic custom shape rather than "this was authored as a
// rect".
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
                                                   const connectivity::ConnectivitySnapshot& nets,
                                                   std::span<const Extension> extensions = {});

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_EXPORT_FOOTPRINT_HPP
