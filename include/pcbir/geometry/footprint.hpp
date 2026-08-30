// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_FOOTPRINT_HPP
#define PCBIR_GEOMETRY_FOOTPRINT_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/point.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pcbir::geometry {

enum class FootprintSide : uint8_t { Top = 0, Bottom = 1 };

// A placed component: groups the Pad/Via entities that belong to one
// physical part (docs/rfcs/0002-kicad-schema-foundation.md). `pads` may mix
// Pad and Via ids (a through-hole pad is modeled as a Via, see via.hpp) --
// well-defined because a Workspace allocates one shared EntityId counter
// across every component table. Member order is preserved on round-trip but,
// unlike connectivity::Bus.members, carries no meaning of its own now that
// each member's own pad_number carries pad identity.
struct Footprint {
  std::string reference_designator;
  std::string value;
  Point position;
  int64_t rotation_e6 = 0; // Degrees * 1e6, extending the project's
                           // dimensionless _e6 fixed-point convention
                           // (Material/ImpedanceProfile) to angular
                           // measure (docs/format-spec.md).
  FootprintSide side = FootprintSide::Top;
  std::vector<core::EntityId> pads;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_FOOTPRINT_HPP
