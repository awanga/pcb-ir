// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_STACKUP_DIAGNOSTICS_HPP
#define PCBIR_STACKUP_DIAGNOSTICS_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/stackup/impedance_profile.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/material.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <vector>

namespace pcbir::stackup {

// Stable diagnostic codes for the stackup layer, in the same spirit as
// pcbir::geometry::DiagnosticCode and pcbir::connectivity::DiagnosticCode:
// once shipped, a code's numeric value never changes (new codes are only
// appended). Its own code space, scoped to stackup -- unifying per-layer
// diagnostic spaces into one is Post-MVP pass-manager work (TASKS.md Phase
// 10), not a Phase 4 requirement.
enum class DiagnosticCode : uint8_t {
  Valid = 0,
  EmptyMaterialName = 1,
  NonPositiveDielectricConstant = 2,   // Material.dielectric_constant_e6 <= 0.
  NegativeLossTangent = 3,             // Material.loss_tangent_e6 < 0.
  NonPositiveLayerThickness = 4,       // Layer.thickness_nm <= 0.
  NegativeLayerRoughness = 5,          // Layer.roughness_nm < 0.
  MissingDielectricMaterial = 6,       // A Dielectric Layer's material is a null EntityId.
  EmptyStackup = 7,                    // A LayerStack has no member layers.
  DuplicateStackupLayer = 8,           // A LayerStack lists the same layer more than once.
  EmptyImpedanceClassName = 9,         // ImpedanceProfile.class_name is empty.
  NonPositiveImpedanceTarget = 10,     // ImpedanceProfile.target_ohm_e6 <= 0.
  DanglingStackupLayerReference = 11,  // A LayerStack member does not resolve to any Layer.
  DanglingLayerMaterialReference = 12, // A Layer's material does not resolve to any Material.
  DanglingViaLayerReference = 13,      // A geometry Via's start/end layer resolves to no Layer.
};

[[nodiscard]] DiagnosticCode validate(const Material& material);
[[nodiscard]] DiagnosticCode validate(const Layer& layer);
[[nodiscard]] DiagnosticCode validate(const LayerStack& stack);
[[nodiscard]] DiagnosticCode validate(const ImpedanceProfile& profile);

// One non-Valid finding from a validation pass, identifying which entity
// produced it.
struct Diagnostic {
  core::EntityId id;
  DiagnosticCode code = DiagnosticCode::Valid;
};

// Runs the per-entity `validate` above over every entity in `snapshot`,
// plus the cross-entity checks that need the whole stackup to decide
// (dangling layer/material references within the stackup itself). Entities
// are visited table-by-table in the snapshot's own canonical (insertion)
// order, so the result is deterministic for a given snapshot.
[[nodiscard]] std::vector<Diagnostic> validate(const StackupSnapshot& snapshot);

// A geometry Via names its stackup layer span by stable EntityId, never by
// geometry (include/pcbir/geometry/via.hpp) -- so a stackup edit that
// removes a referenced layer must be detectable rather than silently
// corrupting the via. This is a separate, opt-in check (rather than folded
// into validate() above) because it is the one stackup diagnostic that
// needs a geometry snapshot as well as a stackup one; a caller that only
// has a StackupSnapshot never needs to pay for it.
[[nodiscard]] std::vector<Diagnostic>
validate_via_layer_references(const geometry::GeometrySnapshot& geometry_snapshot,
                              const StackupSnapshot& stackup_snapshot);

} // namespace pcbir::stackup

#endif // PCBIR_STACKUP_DIAGNOSTICS_HPP
