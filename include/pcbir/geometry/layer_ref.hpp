// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_LAYER_REF_HPP
#define PCBIR_GEOMETRY_LAYER_REF_HPP

#include "pcbir/core/entity_id.hpp"

namespace pcbir::geometry {

// A reference to a stackup layer (the Layer entity itself is defined in
// pcbir::stackup). Board entities that are layer-specific hold one of
// these rather than embedding layer data, per the
// stable-cross-layer-reference design in docs/architecture.md -- a layer
// can be renamed/reordered without invalidating every entity that lives
// on it.
using LayerRef = core::EntityId;

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_LAYER_REF_HPP
