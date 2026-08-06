// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CONNECTIVITY_BUS_HPP
#define PCBIR_CONNECTIVITY_BUS_HPP

#include "pcbir/core/entity_id.hpp"

#include <string>
#include <vector>

namespace pcbir::connectivity {

// A named, ordered group of nets (e.g. a parallel data bus D0-D7). Member
// order carries meaning (bit position within the bus) and is preserved on
// round-trip, unlike a Net's Pin membership, which has no inherent order.
struct Bus {
  std::string name;
  std::vector<core::EntityId> members;
};

} // namespace pcbir::connectivity

#endif // PCBIR_CONNECTIVITY_BUS_HPP
