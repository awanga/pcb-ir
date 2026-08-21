// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_STACKUP_IMPEDANCE_PROFILE_HPP
#define PCBIR_STACKUP_IMPEDANCE_PROFILE_HPP

#include <cstdint>
#include <string>

namespace pcbir::stackup {

// Controlled-impedance data for a net-class or trace-class, identified
// here by name since no NetClass/TraceClass entity exists yet.
// `target_ohm_e6` is the required/specified impedance;
// `actual_ohm_e6` is whatever value is known for it -- fab-declared or
// solver-computed, MVP does not distinguish the source and carries it
// purely as data. Both are ohms scaled by 1e6 (the wire format never
// carries floating point, docs/format-spec.md); full impedance
// computation is Post-MVP (constraint system).
struct ImpedanceProfile {
  std::string class_name;
  int64_t target_ohm_e6 = 0;
  int64_t actual_ohm_e6 = 0;
};

} // namespace pcbir::stackup

#endif // PCBIR_STACKUP_IMPEDANCE_PROFILE_HPP
