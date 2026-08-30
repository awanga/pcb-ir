// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_COPPER_POUR_HPP
#define PCBIR_GEOMETRY_COPPER_POUR_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/polygon.hpp"

namespace pcbir::geometry {

// A filled copper region (a "zone"/"pour"): an outline with optional holes
// (e.g. clearance around unrelated pads/vias) on one layer. `net` is an
// optionally recorded, authored/claimed net (null = unassigned) -- not
// derived or verified against actual geometric connectivity, which is
// Phase 11's routing-graph-extraction concern; it exists so formats that
// attach a net directly to copper (e.g. KiCad's `zone`) round-trip without
// loss (docs/rfcs/0002-kicad-schema-foundation.md). This is the same trust
// boundary connectivity::Pin.net already has, just recorded on the copper
// side too.
struct CopperPour {
  Polygon outline;
  LayerRef layer;
  core::EntityId net;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_COPPER_POUR_HPP
