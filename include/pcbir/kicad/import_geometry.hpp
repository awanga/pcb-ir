// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_IMPORT_GEOMETRY_HPP
#define PCBIR_KICAD_IMPORT_GEOMETRY_HPP

#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

namespace pcbir::kicad {

// Imports the board-outline geometry from a parsed `.kicad_pcb` root
// expression (see parse_sexpr): every `(gr_line ...)`/`(gr_arc ...)` on
// the `Edge.Cuts` layer, assembled into one or more closed BoardOutline
// loops -- a panelized/breakaway board can have more than one disjoint
// outline fragment (geometry/board_outline.hpp). `stackup` must already
// contain the Edge.Cuts Layer (import_stackup's output, pcbir/kicad/
// import.hpp) so each BoardOutline's `layer` field can resolve to it.
//
// A `(gr_arc ...)` is KiCad's three-point (start/mid/end) arc
// parameterization; converting it to PCB-IR's endpoints+center form
// requires computing the circumcenter of the three points, which is not
// exactly representable in integer coordinates in general -- this is a
// documented best-effort floating-point approximation, rounded to the
// nearest nanometre (half away from zero), the same convention
// geometry::rotate's non-90-degree fallback already uses
// (docs/rfcs/0003-kicad-importer-exporter.md).
//
// Only board-outline geometry is imported here -- footprints, pads,
// tracks, vias, and zones are separate, larger pieces of the importer
// (docs/rfcs/0003-kicad-importer-exporter.md).
//
// `base`, if given, seeds the internal GeometryWorkspace (default empty,
// identical to prior behavior) so this pass's entities land in the same
// id space as `base`'s rather than restarting at id 1 -- needed so
// import_kicad_pcb (pcbir/kicad/import.hpp) can chain every composable
// pass into one consistent BoardSnapshot without colliding ids (each
// pass's Workspace otherwise allocates independently from 1; see
// core/workspace.hpp's Workspace(base) constructor).
//
// Throws ImportError (pcbir/kicad/import.hpp) if `stackup` has no
// Edge.Cuts layer, if a `(gr_arc ...)`'s three points are collinear (no
// circumcenter exists), or if the Edge.Cuts edges don't assemble into
// only closed loops (a dangling/open edge is malformed input, never
// silently dropped).
[[nodiscard]] geometry::GeometrySnapshot
import_board_outline(const SExpr& kicad_pcb,
                     const stackup::StackupSnapshot& stackup,
                     const geometry::GeometrySnapshot& base = {});

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_GEOMETRY_HPP
