// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_FIDELITY_HPP
#define PCBIR_KICAD_FIDELITY_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/entity_domain.hpp"

#include <cstdint>
#include <string>
#include <vector>

// The lossiness report generator (docs/rfcs/0003-kicad-importer-exporter.md
// -- "Lossiness report generator", docs/conformance.md -- "Translation-
// fidelity classification"): both import_kicad_pcb and export_kicad_pcb
// (pcbir/kicad/import.hpp, pcbir/kicad/export.hpp) can optionally fill one
// of these in as they run, classifying every entity/attribute they touch
// into exactly one of the four tiers docs/conformance.md already defines.
// No new classification scheme is invented here.
namespace pcbir::kicad {

enum class FidelityTier : uint8_t {
  Preserved,    // Carried through exactly.
  Approximated, // Carried through with a documented, bounded loss.
  Lost,         // Existed in the source, has no PCB-IR/KiCad representation.
  Unsupported,  // Representable in principle, but this pass doesn't handle it yet.
};

// One classified fact: `domain`/`entity_id` name the entity this concerns
// (a null `entity_id` means board-wide, not tied to one specific entity --
// e.g. an unrecognized top-level KiCad section); `entity_kind` is a
// free-form label (e.g. "pad", "footprint", "layers"); `attribute` names
// what specifically was classified (e.g. "shape", "rotation",
// "thru_hole_diameter"); `reason` is a short human-readable explanation,
// always present regardless of tier (a Preserved record's reason is simply
// why it's exact, e.g. "no lossy conversion needed").
struct FidelityRecord {
  EntityDomain domain = EntityDomain::Geometry;
  core::EntityId entity_id;
  std::string entity_kind;
  std::string attribute;
  FidelityTier tier = FidelityTier::Preserved;
  std::string reason;
};

// An ordered list of FidelityRecords for one import/export run. A
// Preserved-only report is the target for the fully in-scope entity set on
// the golden corpus (docs/rfcs/0003-kicad-importer-exporter.md); Lost/
// Unsupported entries are expected for out-of-scope KiCad features and
// must never be silently absent.
struct FidelityReport {
  std::vector<FidelityRecord> records;
};

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_FIDELITY_HPP
