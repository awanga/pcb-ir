// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_IMPORT_ZONE_HPP
#define PCBIR_KICAD_IMPORT_ZONE_HPP

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

namespace pcbir::kicad {

// Imports every `(zone ...)` in a parsed `.kicad_pcb` root expression
// (see parse_sexpr) into one or more geometry::CopperPour entities --
// this pass's scope is the *authored* outline only (a zone's `(polygon
// (pts ...))`), never the computed fill polygons KiCad optionally also
// stores (docs/rfcs/0003-kicad-importer-exporter.md's own open
// question). A zone poured onto several layers at once
// (`(layers "F.Cu" "B.Cu" ...)`, as opposed to a single `(layer "X")`)
// produces one CopperPour per listed layer, each sharing the same
// outline and net -- geometry::CopperPour has exactly one LayerRef, so
// this is the lossless per-layer decomposition of what KiCad treats as
// one multi-layer zone. A zone's first `(polygon (pts ...))` becomes the
// CopperPour's outline; any further ones become holes (a defined-cutout
// zone) -- both canonicalized to Polygon's required winding the same way
// import_board_outline canonicalizes an assembled loop. `stackup` must
// already contain the board's Copper Layers (import_stackup's output);
// `nets` must already contain every Net a zone might reference by name
// (import_nets's output).
//
// `base`, if given, seeds the internal GeometryWorkspace (default empty,
// identical to prior behavior) so this pass's entities land in the same
// id space as `base`'s rather than restarting at id 1 -- needed so
// import_kicad_pcb (pcbir/kicad/import.hpp) can chain every composable
// pass into one consistent BoardSnapshot without colliding ids
// (core/workspace.hpp's Workspace(base) constructor).
//
// Throws ImportError (pcbir/kicad/import.hpp) on a malformed zone
// (missing (layer.../layers ...) or (polygon (pts ...))), a `(pts ...)`
// entry other than `(xy X Y)` (e.g. an arc-cornered zone outline --
// Unsupported for this pass, not silently flattened), or a layer/net
// reference that doesn't resolve against `stackup`/`nets`.
[[nodiscard]] geometry::GeometrySnapshot
import_zones(const SExpr& kicad_pcb,
             const stackup::StackupSnapshot& stackup,
             const connectivity::ConnectivitySnapshot& nets,
             const geometry::GeometrySnapshot& base = {});

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_ZONE_HPP
