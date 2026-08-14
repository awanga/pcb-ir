// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_STACKUP_LAYER_STACK_HPP
#define PCBIR_STACKUP_LAYER_STACK_HPP

#include "pcbir/core/entity_id.hpp"

#include <string>
#include <vector>

namespace pcbir::stackup {

// The board's physical stackup: an ordered, top-to-bottom list of Layer
// references. Order is significant (physical build-up order, and which
// copper layers are adjacent determines valid blind/buried via spans) and
// is preserved on round-trip, mirroring how
// pcbir::connectivity::Bus::members preserves its member ordering.
struct LayerStack {
  std::string name;
  std::vector<core::EntityId> layers;
};

} // namespace pcbir::stackup

#endif // PCBIR_STACKUP_LAYER_STACK_HPP
