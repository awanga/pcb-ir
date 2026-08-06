// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CONNECTIVITY_NET_HPP
#define PCBIR_CONNECTIVITY_NET_HPP

#include <string>

namespace pcbir::connectivity {

// An electrical net: every Pin (pin.hpp) referencing this net's EntityId is
// electrically equipotential. Deliberately geometry-free -- a net's name
// and membership are resolvable without consulting the geometry layer at
// all (docs/architecture.md -- Layer 2, Connectivity).
struct Net {
  std::string name;
};

} // namespace pcbir::connectivity

#endif // PCBIR_CONNECTIVITY_NET_HPP
