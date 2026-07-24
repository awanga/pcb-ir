// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_DIAGNOSTICS_HPP
#define PCBIR_GEOMETRY_DIAGNOSTICS_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/drill_hit.hpp"
#include "pcbir/geometry/keepout.hpp"
#include "pcbir/geometry/mask_opening.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/silkscreen_graphic.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"

#include <cstdint>
#include <vector>

namespace pcbir::geometry {

// Stable diagnostic codes for the geometry layer, in the spirit of a
// compiler's error codes: once shipped, an existing code's numeric value
// never changes (new codes are only appended), so a code is safe to
// store, compare, and document independently of this enum's declaration
// order. See docs/format-spec.md for the full list and meaning of each
// code. Mirrors (and, for entity-specific numeric fields, extends)
// ContourValidity/PolygonValidity/PathValidity -- this is the one unified
// code space a caller checks across every board-entity type, rather than
// switching on each entity's own per-primitive validity enum.
enum class DiagnosticCode : uint8_t {
  Valid = 0,
  InvalidOutline = 1,
  OutlineWrongOrientation = 2,
  InvalidHole = 3,
  HoleWrongOrientation = 4,
  HoleOutsideOutline = 5,
  EmptyPath = 6,
  DiscontinuousPath = 7,
  InvalidArcSpan = 8,
  NonPositiveSpanWidth = 9,
  NonPositiveDrillDiameter = 10,
  NonPositiveFinishedHoleDiameter = 11,
  NonPositivePadDiameter = 12,
  NonPositiveAnnularRing = 13,
};

[[nodiscard]] DiagnosticCode validate(const Pad& pad);
[[nodiscard]] DiagnosticCode validate(const Via& via);
[[nodiscard]] DiagnosticCode validate(const Track& track);
[[nodiscard]] DiagnosticCode validate(const CopperPour& pour);
[[nodiscard]] DiagnosticCode validate(const Keepout& keepout);
[[nodiscard]] DiagnosticCode validate(const DrillHit& hit);
[[nodiscard]] DiagnosticCode validate(const MaskOpening& opening);
[[nodiscard]] DiagnosticCode validate(const SilkscreenGraphic& graphic);

// One non-Valid finding from a validation pass, identifying which entity
// produced it.
struct Diagnostic {
  core::EntityId id;
  DiagnosticCode code = DiagnosticCode::Valid;
};

// Runs the per-entity `validate` above over every entity in `snapshot`
// (every board-entity table geometry defines), returning one Diagnostic
// per entity whose code is not Valid. Entities are visited table-by-table
// in the snapshot's own canonical (insertion) order, then within each
// table in that table's iteration order, so the result is deterministic
// for a given snapshot.
[[nodiscard]] std::vector<Diagnostic> validate(const GeometrySnapshot& snapshot);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_DIAGNOSTICS_HPP
