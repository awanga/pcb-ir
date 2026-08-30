// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_TRACK_HPP
#define PCBIR_GEOMETRY_TRACK_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/path.hpp"

namespace pcbir::geometry {

// A copper trace: an ordered polyline of segment/arc spans, each with its
// own width, on one layer. `net` is an optionally recorded, authored/claimed
// net (null = unassigned) -- not derived or verified against actual
// geometric connectivity, which is Phase 11's routing-graph-extraction
// concern; it exists so formats that attach a net directly to copper (e.g.
// KiCad's `segment`) round-trip without loss
// (docs/rfcs/0002-kicad-schema-foundation.md). This is the same trust
// boundary connectivity::Pin.net already has, just recorded on the copper
// side too.
struct Track {
  Path path;
  LayerRef layer;
  core::EntityId net;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_TRACK_HPP
