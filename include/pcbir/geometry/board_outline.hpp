// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_BOARD_OUTLINE_HPP
#define PCBIR_GEOMETRY_BOARD_OUTLINE_HPP

#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/polygon.hpp"

namespace pcbir::geometry {

// The board's mechanical boundary: an outline with optional holes (interior
// cutouts/slots) on a mechanical (not physical-stackup) layer -- `layer`
// must resolve to a stackup Layer{kind=EdgeCuts}
// (docs/rfcs/0002-kicad-schema-foundation.md). Structurally identical to
// CopperPour/Keepout/MaskOpening; a separate component table purely for
// semantic distinction, the same way Track/SilkscreenGraphic are. Zero or
// more per board: a panelized/breakaway board has genuinely disjoint
// outline fragments.
struct BoardOutline {
  Polygon outline;
  LayerRef layer;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_BOARD_OUTLINE_HPP
