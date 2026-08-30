// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_VIA_HPP
#define PCBIR_GEOMETRY_VIA_HPP

#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/point.hpp"

#include <cstdint>
#include <string>

namespace pcbir::geometry {

// A plated hole spanning from `start_layer` to `end_layer`. A through-hole
// via is the degenerate case where the span covers every layer; blind
// (outer-to-inner) and buried (inner-to-inner) vias are the same type with
// a narrower span -- layers are referenced by stable LayerRef, never by
// geometry, so a stackup edit can be validated against dangling
// references (docs/architecture.md) rather than silently corrupting a
// via's span.
struct Via {
  Point position;
  int64_t drill_diameter_nm = 0;
  int64_t finished_hole_diameter_nm = 0;
  int64_t pad_diameter_nm = 0; // Outer diameter of the copper annular ring.
  LayerRef start_layer;
  LayerRef end_layer;

  // The footprint-local pad identifier this via stands in for when it is a
  // plated through-hole pad (see Pad's own pad_number) -- empty for a via
  // that is not part of any Footprint (e.g. a free-floating stitching via).
  std::string pad_number;

  // Derived, not stored, so it can never drift out of sync with
  // pad_diameter_nm/finished_hole_diameter_nm.
  [[nodiscard]] int64_t annular_ring_nm() const {
    return (pad_diameter_nm - finished_hole_diameter_nm) / 2;
  }
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_VIA_HPP
