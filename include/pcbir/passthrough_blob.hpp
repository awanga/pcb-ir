// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_PASSTHROUGH_BLOB_HPP
#define PCBIR_PASSTHROUGH_BLOB_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/entity_domain.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pcbir {

// Opaque, per-entity raw bytes for source-format data an importer
// recognizes syntactically but cannot semantically map into the IR.
// Distinct from Extension: never typed or interpreted by PCB-IR,
// round-tripped verbatim back to `source_format` on export.
struct PassthroughBlob {
  EntityDomain domain = EntityDomain::Geometry;
  core::EntityId entity_id;
  std::string source_format;
  std::vector<uint8_t> data;
};

} // namespace pcbir

#endif // PCBIR_PASSTHROUGH_BLOB_HPP
