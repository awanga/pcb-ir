// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_IMPORT_HPP
#define PCBIR_KICAD_IMPORT_HPP

#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

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
// one LayerStack listing just the Copper layers in KiCad's own numeric
// layer-id order -- which is physical top-to-bottom stackup order for
// copper layers (verified against real pcbnew output; see
// docs/rfcs/0003-kicad-importer-exporter.md). KiCad's other "technical"/
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

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_IMPORT_HPP
