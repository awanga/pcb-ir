// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_IMPORT_NETS_HPP
#define PCBIR_KICAD_IMPORT_NETS_HPP

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"

namespace pcbir::kicad {

// Imports every net referenced anywhere in a parsed `.kicad_pcb` root
// expression (see parse_sexpr) into a connectivity snapshot: one
// connectivity::Net per distinct name found in a `(net "NAME")` entry
// (a pad, via, track segment/arc, or zone's net assignment), in sorted
// name order for a deterministic result independent of file traversal
// order. KiCad 10's board format has no on-disk net-code table -- a bare
// name string is a copper item's only net identity (verified against real
// pcbnew output; see docs/rfcs/0003-kicad-importer-exporter.md), so this
// is the importer's only pass needed to establish net identity.
//
// Produces no connectivity::Pin entries -- linking a specific Pad/Via to
// one of these Net entities happens where that Pad/Via is itself created
// (pcbir/kicad/import_footprint.hpp), which needs this snapshot's Net ids
// as an input to do so.
[[nodiscard]] connectivity::ConnectivitySnapshot import_nets(const SExpr& kicad_pcb);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_NETS_HPP
