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
// Throws ImportError (pcbir/kicad/import.hpp) on a malformed via (missing
// (at/size/drill/layers ...)) or a layer/net reference that doesn't
// resolve against `stackup`/`nets`.
[[nodiscard]] ViaImportResult import_vias(const SExpr& kicad_pcb,
                                          const stackup::StackupSnapshot& stackup,
                                          const connectivity::ConnectivitySnapshot& nets);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_VIA_HPP
