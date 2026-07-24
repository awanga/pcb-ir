// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_KEEPOUT_HPP
#define PCBIR_GEOMETRY_KEEPOUT_HPP

#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/polygon.hpp"

namespace pcbir::geometry {

// A region where placement/routing/copper is restricted, on one layer.
// *What* is restricted (copper, vias, routing, courtyard, ...) is a
// constraint-system concern (Post-MVP, see TASKS.md); this is the pure
// geometric footprint of the restricted area.
struct Keepout {
  Polygon outline;
  LayerRef layer;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_KEEPOUT_HPP
