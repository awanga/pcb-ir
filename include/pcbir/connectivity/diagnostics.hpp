// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CONNECTIVITY_DIAGNOSTICS_HPP
#define PCBIR_CONNECTIVITY_DIAGNOSTICS_HPP

#include "pcbir/connectivity/bus.hpp"
#include "pcbir/connectivity/differential_pair.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"

#include <cstdint>
#include <vector>

namespace pcbir::connectivity {

// Stable diagnostic codes for the connectivity layer, in the same spirit as
// pcbir::geometry::DiagnosticCode: once shipped, a code's numeric value
// never changes (new codes are only appended). This is its own code space,
// scoped to connectivity rather than shared with geometry's -- unifying
// per-layer diagnostic spaces into one is Post-MVP pass-manager work
// (TASKS.md Phase 10), not a Phase 3 requirement.
enum class DiagnosticCode : uint8_t {
  Valid = 0,
  EmptyNetName = 1,
  InvalidPin = 2,              // Pin.pad is a null EntityId.
  UnconnectedPin = 3,          // Pin.net is a null EntityId (an orphan pin).
  DanglingPinNetReference = 4, // Pin.net does not resolve to any Net in the snapshot.
  DuplicatePadAssignment = 5,  // The same pad is referenced by more than one Pin (a short).
  DegenerateDiffPair = 6,      // A member net is null, or both members are the same net.
  DanglingDiffPairMember = 7,  // A diff pair member net does not resolve to any Net.
  EmptyBus = 8,                // A Bus has no member nets.
  DuplicateBusMember = 9,      // A Bus lists the same net more than once.
  DanglingBusMember = 10,      // A Bus member net does not resolve to any Net.
};

[[nodiscard]] DiagnosticCode validate(const Net& net);
[[nodiscard]] DiagnosticCode validate(const Pin& pin);
[[nodiscard]] DiagnosticCode validate(const DifferentialPair& pair);
[[nodiscard]] DiagnosticCode validate(const Bus& bus);

// One non-Valid finding from a validation pass, identifying which entity
// produced it.
struct Diagnostic {
  core::EntityId id;
  DiagnosticCode code = DiagnosticCode::Valid;
};

// Runs the per-entity `validate` above over every entity in `snapshot`,
// plus the cross-entity checks that need the whole net graph to decide
// (dangling net references, duplicate pad assignment across Pins, and
// dangling diff-pair/bus members) -- checks no single entity's own fields
// can answer in isolation. Entities are visited table-by-table in the
// snapshot's own canonical (insertion) order, so the result is
// deterministic for a given snapshot.
[[nodiscard]] std::vector<Diagnostic> validate(const ConnectivitySnapshot& snapshot);

} // namespace pcbir::connectivity

#endif // PCBIR_CONNECTIVITY_DIAGNOSTICS_HPP
