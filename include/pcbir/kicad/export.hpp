// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_EXPORT_HPP
#define PCBIR_KICAD_EXPORT_HPP

#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <filesystem>
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

// Exports every Track in `geometry` into one `(segment ...)`/`(arc ...)`
// SExpr node per WidthSpan in its Path -- the exporter's inverse of
// import_tracks (pcbir/kicad/import_track.hpp). `net`, if not null, is
// resolved to its Net's name and emitted as `(net "NAME")`; a null net is
// omitted entirely, matching how KiCad itself never writes a placeholder
// for an unclaimed net. Each node's UUIDv5 is derived from its owning
// Track's EntityId and its span's position within the Path.
//
// Throws ExportError if a Track's `layer` doesn't resolve against
// `stackup`, or its `net` doesn't resolve against `nets`.
[[nodiscard]] std::vector<SExpr> export_tracks(const geometry::GeometrySnapshot& geometry,
                                               const stackup::StackupSnapshot& stackup,
                                               const connectivity::ConnectivitySnapshot& nets);

// Exports every Via in `geometry` that isn't referenced by any
// Footprint's `pads` list (a free-standing/stitching via, not a
// footprint's thru-hole pad -- pcbir/kicad/import_footprint.hpp handles
// those on import, and the eventual export_footprints will handle them on
// export) into one `(via ...)` SExpr node -- the exporter's inverse of
// import_vias (pcbir/kicad/import_via.hpp). `start_layer`/`end_layer` are
// always emitted as two explicit layer names (never KiCad's `"*.Cu"`
// wildcard); both forms are semantically identical to pcbnew, so this
// loses no information, only the original text form of a wildcard span.
//
// Throws ExportError if a free Via's `start_layer`/`end_layer` doesn't
// resolve against `stackup`, or its `net` doesn't resolve against `nets`.
[[nodiscard]] std::vector<SExpr> export_vias(const geometry::GeometrySnapshot& geometry,
                                             const stackup::StackupSnapshot& stackup,
                                             const connectivity::ConnectivitySnapshot& nets);

// Exports every CopperPour in `geometry` into one `(zone ...)` SExpr node
// -- the exporter's inverse of import_zones (pcbir/kicad/import_zone.hpp).
// Unlike import_zones (which splits one multi-layer `(layers "X" "Y" ...)`
// zone into several single-layer CopperPours sharing an outline), this
// always emits a single-layer `(layer "X")` zone per CopperPour, even when
// several CopperPours happen to share the same outline and net -- merging
// them back into one multi-layer zone would require detecting that
// equivalence, which this first pass doesn't attempt. The result is still
// geometrically and electrically identical once reloaded (a documented
// Approximated-tier restructuring: zone *count* isn't preserved, only the
// copper each zone pours). `net`, if not null, is resolved to its Net's
// name and emitted as `(net "NAME")`; a null net omits the field.
//
// Throws ExportError if a CopperPour's `layer` doesn't resolve against
// `stackup`, its `net` doesn't resolve against `nets`, or its outline or
// any hole contains an Arc span (KiCad's `(zone ...)` `(polygon (pts
// ...))` supports only straight-edge points -- the same restriction
// import_zones documents as Unsupported for an arc-cornered zone).
[[nodiscard]] std::vector<SExpr> export_zones(const geometry::GeometrySnapshot& geometry,
                                              const stackup::StackupSnapshot& stackup,
                                              const connectivity::ConnectivitySnapshot& nets);

// The top-level orchestrator: assembles every per-entity export_* piece's
// nodes into one `(kicad_pcb ...)` root expression -- version/generator/
// general/paper/layers/setup wrapper fields (verified against a real
// pcbnew-authored board, tests/corpus/kicad/rounded-rect-outline/
// board.kicad_pcb), then board outline, footprints, tracks, free vias, and
// zones in that order, matching real pcbnew's own section ordering -- and
// writes the result to `path` via write_sexpr (pcbir/kicad/sexpr.hpp).
// `board.extensions`/`board.passthrough_blobs` have no KiCad export path
// yet and are ignored. The importer's inverse: import_kicad_pcb
// (pcbir/kicad/import.hpp).
//
// Throws ExportError for anything any individual export_* piece above
// would throw for this snapshot's content, or if `path` can't be opened
// for writing.
void export_kicad_pcb(const pcbir::BoardSnapshot& board, const std::filesystem::path& path);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_EXPORT_HPP
