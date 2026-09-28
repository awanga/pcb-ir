// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_EXPORT_HPP
#define PCBIR_KICAD_EXPORT_HPP

#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace pcbir::kicad {

// Thrown for a semantic KiCad-export error: the input snapshot is
// internally inconsistent in a way that can't be rendered as a valid
// `.kicad_pcb` (a dangling LayerRef, a LayerStack too long for KiCad's
// fixed copper-layer-id scheme). Distinct from ImportError
// (pcbir/kicad/import.hpp), which reports the opposite direction's
// failures.
class ExportError : public std::runtime_error {
public:
  explicit ExportError(const std::string& message) : std::runtime_error(message) {}
};

// Exports `stackup`'s Copper/EdgeCuts/Silkscreen layers into a
// `(layers ...)` SExpr node -- the exporter's inverse of import_stackup
// (pcbir/kicad/import.hpp). Copper layers are emitted in the stackup's own
// LayerStack order (front to back) using KiCad's own fixed numeric layer
// ids (F.Cu = 0, B.Cu = 2 always, inner layers 4, 6, 8, ... in physical
// order) -- verified against real pcbnew output on 2/4/6-copper-layer
// boards (docs/rfcs/0003-kicad-importer-exporter.md); confirmed separately
// that pcbnew re-derives a layer's canonical id from its name on load
// rather than trusting the file's stored id, so this scheme need not be
// bit-exact to satisfy a real reload, but matching it keeps exported files
// close to what a real pcbnew save would produce. Edge.Cuts (id 25) and
// F.SilkS/B.SilkS (ids 5/7) use their own fixed ids, matching real pcbnew
// output too. Every other KiCad "technical"/"user" layer (F.Mask, F.Paste,
// F.Fab, F.CrtYd, Dwgs.User, ...) has no stackup::LayerKind yet
// (import.hpp) and so is never emitted, the same asymmetric coverage
// import_stackup already has.
//
// Throws ExportError if a LayerStack entry doesn't resolve to a Layer in
// `stackup`, or if the LayerStack has more than 32 entries (KiCad's own
// fixed copper-layer-id scheme has no id left to assign beyond that).
[[nodiscard]] SExpr export_layers_section(const stackup::StackupSnapshot& stackup);

// Exports every BoardOutline in `geometry` into one `(gr_line ...)`/
// `(gr_arc ...)` SExpr node per span, on the Edge.Cuts layer -- the
// exporter's inverse of import_board_outline (pcbir/kicad/
// import_geometry.hpp). Each node carries a UUIDv5 derived from its
// BoardOutline's EntityId and its position within that outline's span
// list (docs/format-spec.md's "KiCad export UUID namespace"), so the same
// snapshot always exports to the same UUIDs. `stackup` must already
// contain the board's Edge.Cuts Layer (whatever produced `geometry` is
// expected to have run import_stackup/an equivalent first, the same
// precondition import_board_outline itself has).
//
// Throws ExportError if `stackup` has no Edge.Cuts layer, or if a
// BoardOutline's own `layer` doesn't resolve against it.
[[nodiscard]] std::vector<SExpr> export_board_outline(const geometry::GeometrySnapshot& geometry,
                                                      const stackup::StackupSnapshot& stackup);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_EXPORT_HPP
