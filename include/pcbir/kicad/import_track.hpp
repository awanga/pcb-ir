// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_IMPORT_TRACK_HPP
#define PCBIR_KICAD_IMPORT_TRACK_HPP

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

namespace pcbir::kicad {

// Imports every top-level `(segment ...)`/`(arc ...)` in a parsed
// `.kicad_pcb` root expression (see parse_sexpr) into a geometry::Track,
// one per KiCad entry -- KiCad doesn't group related segments/arcs into
// any larger structure on disk, so this doesn't attempt to merge adjacent
// same-net/same-layer spans into a multi-span Track; each becomes its own
// Track with a single-span Path (docs/rfcs/0003-kicad-importer-exporter.md).
// A `(arc ...)` uses the same three-point (start/mid/end) parameterization
// as `(gr_arc ...)` (pcbir/kicad/import_geometry.hpp) and is converted the
// same best-effort way. `stackup` must already contain the board's Copper
// Layers (import_stackup's output); `nets` must already contain every Net
// a track might reference by name (import_nets's output).
//
// Throws ImportError (pcbir/kicad/import.hpp) on a malformed entry
// (missing (start/end/width/layer ...)) or a layer/net reference that
// doesn't resolve against `stackup`/`nets`.
[[nodiscard]] geometry::GeometrySnapshot
import_tracks(const SExpr& kicad_pcb,
              const stackup::StackupSnapshot& stackup,
              const connectivity::ConnectivitySnapshot& nets);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_TRACK_HPP
