// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_STACKUP_LAYER_HPP
#define PCBIR_STACKUP_LAYER_HPP

#include "pcbir/core/entity_id.hpp"

#include <cstdint>
#include <string>

namespace pcbir::stackup {

// EdgeCuts is not a physical layer -- it is the mechanical/drafting layer a
// board outline (pcbir::geometry::BoardOutline) references, carrying no
// thickness/roughness/material (docs/rfcs/0002-kicad-schema-foundation.md).
// Only Copper/Dielectric participate in the physical stackup (LayerStack).
enum class LayerKind : uint8_t { Copper = 0, Dielectric = 1, EdgeCuts = 2 };

// One layer in the board's layer-identity space. Only Copper/Dielectric are
// physical layers in the board's cross-section; thickness_nm/roughness_nm
// reuse the project-wide nanometer length convention (docs/format-spec.md);
// roughness is copper foil profile (Rz) and is unused (left 0) for
// Dielectric and EdgeCuts layers. `material` is the owning Material's
// stable EntityId -- meaningful (and required, see pcbir::stackup::validate)
// only for a Dielectric layer; null for Copper and EdgeCuts, which have no
// variable dielectric properties to record.
struct Layer {
  std::string name;
  LayerKind kind = LayerKind::Copper;
  int64_t thickness_nm = 0;
  int64_t roughness_nm = 0;
  core::EntityId material;
};

} // namespace pcbir::stackup

#endif // PCBIR_STACKUP_LAYER_HPP
