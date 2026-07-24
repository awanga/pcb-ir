// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_SILKSCREEN_GRAPHIC_HPP
#define PCBIR_GEOMETRY_SILKSCREEN_GRAPHIC_HPP

#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/path.hpp"

namespace pcbir::geometry {

// A silkscreen (legend) stroke: geometrically the same shape as a Track
// (a per-span-width polyline), but on a silkscreen layer with no
// electrical meaning. Text is out of scope for v0.1 (TASKS.md); a
// silkscreen font-text entity would layer on top of this, not replace it.
struct SilkscreenGraphic {
  Path path;
  LayerRef layer;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_SILKSCREEN_GRAPHIC_HPP
