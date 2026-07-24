// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_MASK_OPENING_HPP
#define PCBIR_GEOMETRY_MASK_OPENING_HPP

#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/polygon.hpp"

namespace pcbir::geometry {

// An opening in the solder mask (or paste layer), exposing copper
// underneath -- typically larger than the pad it exposes, but stored as
// its own exact outline rather than derived/implied from a pad.
struct MaskOpening {
  Polygon outline;
  LayerRef layer;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_MASK_OPENING_HPP
