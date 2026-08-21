// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_COPPER_POUR_HPP
#define PCBIR_GEOMETRY_COPPER_POUR_HPP

#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/polygon.hpp"

namespace pcbir::geometry {

// A filled copper region (a "zone"/"pour"): an outline with optional holes
// (e.g. clearance around unrelated pads/vias) on one layer. Net membership
// is a connectivity concern (pcbir::connectivity) and is not modeled here.
struct CopperPour {
  Polygon outline;
  LayerRef layer;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_COPPER_POUR_HPP
