// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_IMPORT_HPP
#define PCBIR_KICAD_IMPORT_HPP

#include "pcbir/board_snapshot.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace pcbir::kicad {

// Thrown for a semantic KiCad-import error: the input parsed as valid
// S-expression syntax (sexpr.hpp) but doesn't have the structure a
// `.kicad_pcb` file is required to have (missing required section, a
// malformed field where a specific shape/type was expected). Distinct
// from SExprParseError, which is purely syntactic.
class ImportError : public std::runtime_error {
public:
  explicit ImportError(const std::string& message) : std::runtime_error(message) {}
};

// Imports the `(layers ...)` section of a parsed `.kicad_pcb` root
// expression (see parse_sexpr) into a stackup snapshot: one Layer per
// Copper (name ends in ".Cu"), Edge.Cuts, or F.SilkS/B.SilkS entry, plus
// one LayerStack listing just the Copper layers in the order they appear
// in the file -- which is physical top-to-bottom stackup order for copper
// layers, NOT their numeric layer-id order (a board's B.Cu always carries
// the fixed id 2 regardless of copper layer count, while inner layers get
// higher ids in physical order, so id order and file order only coincide
// for a 2-layer board; verified against real pcbnew output on 2/4/6-layer
// boards -- see docs/rfcs/0003-kicad-importer-exporter.md). KiCad's other
// "technical"/
// "user" layers (F.Mask, F.Paste, F.Fab, F.CrtYd, Dwgs.User, ...) have no
// stackup::LayerKind yet and are silently skipped, not imported
// (docs/rfcs/0003-kicad-importer-exporter.md -- "Explicitly out of
// scope"). A `.kicad_pcb`'s `(layers ...)` section carries no per-layer
// thickness/material data (that lives in the optional, not yet imported,
// `(setup (stackup ...))` section) -- every imported Copper Layer gets a
// fixed default thickness so it satisfies stackup::validate(), reported
// as Approximated (not Preserved) in the eventual fidelity report.
//
// Throws ImportError if `kicad_pcb` has no `(layers ...)` section at all.
[[nodiscard]] stackup::StackupSnapshot import_stackup(const SExpr& kicad_pcb);

// The top-level orchestrator: reads `path`, parses it (see parse_sexpr),
// and assembles the composable per-entity passes (import_stackup,
// import_nets, import_board_outline, import_footprints, import_tracks,
// import_vias, import_zones) into one consistent pcbir::BoardSnapshot,
// threading each pass's geometry/connectivity output into the next as its
// `base`/`nets` argument (pcbir/kicad/import_geometry.hpp and siblings)
// so every entity across every pass lands in one shared id space rather
// than colliding (each pass's Workspace otherwise starts counting from 1
// independently -- see core/workspace.hpp's Workspace(base) constructor).
// Passes run in the fixed order: board outline, footprints, tracks, free
// vias, zones. `BoardSnapshot::extensions` collects every
// PCBIR_KICAD/pad_shape extension import_footprints produced
// (pcbir/kicad/import_footprint.hpp); `passthrough_blobs` is always empty
// (this importer has no opaque-passthrough concept yet).
//
// Throws ImportError if `path` can't be opened/read, if its content isn't
// a `(kicad_pcb ...)` root expression, or if its `(version ...)` is below
// this importer's pinned minimum (docs/rfcs/0003-kicad-importer-exporter.md
// -- "Target version pin"; older KiCad versions are out of scope, rejected
// with a diagnostic rather than best-effort parsed) -- or anything any
// individual pass above would throw for this file's content.
[[nodiscard]] pcbir::BoardSnapshot import_kicad_pcb(const std::filesystem::path& path);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_HPP
