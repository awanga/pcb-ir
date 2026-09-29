// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_IMPORT_VIA_HPP
#define PCBIR_KICAD_IMPORT_VIA_HPP

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

namespace pcbir::kicad {

// The two-layer output of import_vias, mirroring
// FootprintImportResult (pcbir/kicad/import_footprint.hpp): geometry
// (Via) and connectivity (the input `nets` snapshot's Net table, carried
// forward unchanged, plus one new Pin per Via created here).
struct ViaImportResult {
  geometry::GeometrySnapshot geometry;
  connectivity::ConnectivitySnapshot connectivity;
};

// Imports every top-level `(via ...)` (a free-standing/stitching via, not
// a footprint's thru-hole pad -- pcbir/kicad/import_footprint.hpp handles
// those) in a parsed `.kicad_pcb` root expression into a geometry::Via
// plus a linking connectivity::Pin. `stackup` must already contain the
// board's Copper Layers and LayerStack (import_stackup's output); `nets`
// must already contain every Net a via might reference by name
// (import_nets's output).
//
// KiCad's optional blind/microvia type keyword (`(via blind ...)`,
// `(via micro ...)`) is parsed structurally (it doesn't change where
// (at/size/drill/layers/net ...) live) but not itself recorded --
// geometry::Via has no blind/microvia distinction to carry it in
// (a known, small fidelity gap, not yet addressed by this pass).
//
// `geometry_base`, if given, seeds the internal GeometryWorkspace (default
// empty, identical to prior behavior) so this pass's entities land in the
// same id space as `geometry_base`'s rather than restarting at id 1 --
// needed so import_kicad_pcb (pcbir/kicad/import.hpp) can chain every
// composable pass into one consistent BoardSnapshot (core/workspace.hpp's
// Workspace(base) constructor). `nets` already doubles as the connectivity
// accumulation base the same way import_footprints' does (pcbir/kicad/
// import_footprint.hpp): pass an already-accumulated connectivity snapshot
// (e.g. import_footprints' own output) rather than import_nets's bare
// output to continue Pin id allocation from where that pass left off.
//
// Throws ImportError (pcbir/kicad/import.hpp) on a malformed via (missing
// (at/size/drill/layers ...)) or a layer/net reference that doesn't
// resolve against `stackup`/`nets`.
[[nodiscard]] ViaImportResult import_vias(const SExpr& kicad_pcb,
                                          const stackup::StackupSnapshot& stackup,
                                          const connectivity::ConnectivitySnapshot& nets,
                                          const geometry::GeometrySnapshot& geometry_base = {});

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_VIA_HPP
