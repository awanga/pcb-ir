// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_ENTITY_DOMAIN_HPP
#define PCBIR_ENTITY_DOMAIN_HPP

#include <cstdint>

namespace pcbir {

// Which layer an Extension/PassthroughBlob's entity_id resolves against.
// Each layer's Workspace allocates its own EntityId space independently
// (docs/architecture.md), so a bare EntityId is ambiguous across layers --
// (domain, entity_id) together are not. Append-only, the same convention
// as every DiagnosticCode enum (docs/format-spec.md).
enum class EntityDomain : uint8_t {
  Geometry = 0,
  Connectivity = 1,
  Stackup = 2,
};

} // namespace pcbir

#endif // PCBIR_ENTITY_DOMAIN_HPP
