// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CONNECTIVITY_PIN_HPP
#define PCBIR_CONNECTIVITY_PIN_HPP

#include "pcbir/core/entity_id.hpp"

namespace pcbir::connectivity {

// The connectivity layer's node: a connection point on a board, tying a
// geometry-layer pad/via (referenced by its stable EntityId, never
// embedded -- Connectivity stays independent of Geometry) to at most one
// Net. `net` is a null EntityId for an unconnected pin (an orphan --
// flagged by validate(), never silently dropped).
struct Pin {
  core::EntityId pad;
  core::EntityId net;
};

} // namespace pcbir::connectivity

#endif // PCBIR_CONNECTIVITY_PIN_HPP
