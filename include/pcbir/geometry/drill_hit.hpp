// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_DRILL_HIT_HPP
#define PCBIR_GEOMETRY_DRILL_HIT_HPP

#include "pcbir/geometry/point.hpp"

#include <cstdint>

namespace pcbir::geometry {

// A standalone drilled hole (e.g. a mechanical mounting hole) that is not
// itself an electrical via -- a hole that plates and spans specific
// stackup layers is a Via, not a DrillHit.
struct DrillHit {
  Point position;
  int64_t diameter_nm = 0;
  bool plated = false;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_DRILL_HIT_HPP
