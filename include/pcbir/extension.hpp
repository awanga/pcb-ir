// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_EXTENSION_HPP
#define PCBIR_EXTENSION_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/entity_domain.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pcbir {

// A namespaced, typed, versioned piece of data attached to one entity,
// without requiring a breaking schema change to add
// (docs/extensions-governance.md). `ext_namespace` is one of VENDOR_*,
// EXT_*, or a registered PCBIR_* name; `name` identifies the extension
// within that namespace. A reader that doesn't recognize
// (ext_namespace, name) safely ignores `payload` -- the entry itself still
// round-trips unchanged.
struct Extension {
  EntityDomain domain = EntityDomain::Geometry;
  core::EntityId entity_id;
  std::string ext_namespace;
  std::string name;
  uint32_t version = 0;
  std::vector<uint8_t> payload;
};

} // namespace pcbir

#endif // PCBIR_EXTENSION_HPP
