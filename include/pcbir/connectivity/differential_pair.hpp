// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CONNECTIVITY_DIFFERENTIAL_PAIR_HPP
#define PCBIR_CONNECTIVITY_DIFFERENTIAL_PAIR_HPP

#include "pcbir/core/entity_id.hpp"

namespace pcbir::connectivity {

// Two nets driven as a differential signal. Which field a net occupies is
// its polarity -- positive_net/negative_net are distinct fields rather
// than a (net, polarity-enum) pair, so there is nothing that could drift
// out of sync on round-trip.
struct DifferentialPair {
  core::EntityId positive_net;
  core::EntityId negative_net;
};

} // namespace pcbir::connectivity

#endif // PCBIR_CONNECTIVITY_DIFFERENTIAL_PAIR_HPP
