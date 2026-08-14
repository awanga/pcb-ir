// SPDX-License-Identifier: Apache-2.0
#include "pcbir/stackup/diagnostics.hpp"

#include "pcbir/core/arena.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/stackup/impedance_profile.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/material.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <vector>

namespace pcbir::stackup {

namespace {

template <typename T> void collect(const StackupSnapshot& snapshot, std::vector<Diagnostic>& out) {
  snapshot.template table<T>().for_each([&out](core::EntityId id, const T& entity) {
    const DiagnosticCode code = validate(entity);
    if (code != DiagnosticCode::Valid) {
      out.push_back(Diagnostic{.id = id, .code = code});
    }
  });
}

} // namespace

DiagnosticCode validate(const Material& material) {
  if (material.name.empty()) {
    return DiagnosticCode::EmptyMaterialName;
  }
  if (material.dielectric_constant_e6 <= 0) {
    return DiagnosticCode::NonPositiveDielectricConstant;
  }
  if (material.loss_tangent_e6 < 0) {
    return DiagnosticCode::NegativeLossTangent;
  }
  return DiagnosticCode::Valid;
}

DiagnosticCode validate(const Layer& layer) {
  if (layer.thickness_nm <= 0) {
    return DiagnosticCode::NonPositiveLayerThickness;
  }
  if (layer.roughness_nm < 0) {
    return DiagnosticCode::NegativeLayerRoughness;
  }
  if (layer.kind == LayerKind::Dielectric && layer.material.is_null()) {
    return DiagnosticCode::MissingDielectricMaterial;
  }
  return DiagnosticCode::Valid;
}

DiagnosticCode validate(const LayerStack& stack) {
  if (stack.layers.empty()) {
    return DiagnosticCode::EmptyStackup;
  }
  // EntityId only exposes `==`/`<` (entity_id.hpp), not the full
  // totally_ordered set std::ranges::less requires -- sort by its
  // underlying value instead of the handle type itself.
  std::vector<core::EntityId> sorted = stack.layers;
  std::ranges::sort(sorted, {}, &core::EntityId::value);
  if (std::ranges::adjacent_find(sorted) != sorted.end()) {
    return DiagnosticCode::DuplicateStackupLayer;
  }
  return DiagnosticCode::Valid;
}

DiagnosticCode validate(const ImpedanceProfile& profile) {
  if (profile.class_name.empty()) {
    return DiagnosticCode::EmptyImpedanceClassName;
  }
  if (profile.target_ohm_e6 <= 0) {
    return DiagnosticCode::NonPositiveImpedanceTarget;
  }
  return DiagnosticCode::Valid;
}

std::vector<Diagnostic> validate(const StackupSnapshot& snapshot) {
  std::vector<Diagnostic> result;
  collect<Material>(snapshot, result);
  collect<Layer>(snapshot, result);
  collect<LayerStack>(snapshot, result);
  collect<ImpedanceProfile>(snapshot, result);

  const core::Arena<Layer>& layers = snapshot.table<Layer>();
  const core::Arena<Material>& materials = snapshot.table<Material>();

  snapshot.table<Layer>().for_each([&](core::EntityId id, const Layer& layer) {
    if (!layer.material.is_null() && materials.find(layer.material).is_null()) {
      result.push_back(
          Diagnostic{.id = id, .code = DiagnosticCode::DanglingLayerMaterialReference});
    }
  });

  snapshot.table<LayerStack>().for_each([&](core::EntityId id, const LayerStack& stack) {
    for (const core::EntityId& member : stack.layers) {
      if (layers.find(member).is_null()) {
        result.push_back(
            Diagnostic{.id = id, .code = DiagnosticCode::DanglingStackupLayerReference});
        break;
      }
    }
  });

  return result;
}

std::vector<Diagnostic>
validate_via_layer_references(const geometry::GeometrySnapshot& geometry_snapshot,
                              const StackupSnapshot& stackup_snapshot) {
  std::vector<Diagnostic> result;
  const core::Arena<Layer>& layers = stackup_snapshot.table<Layer>();

  geometry_snapshot.table<geometry::Via>().for_each(
      [&](core::EntityId id, const geometry::Via& via) {
        const bool start_dangling =
            !via.start_layer.is_null() && layers.find(via.start_layer).is_null();
        const bool end_dangling = !via.end_layer.is_null() && layers.find(via.end_layer).is_null();
        if (start_dangling || end_dangling) {
          result.push_back(Diagnostic{.id = id, .code = DiagnosticCode::DanglingViaLayerReference});
        }
      });

  return result;
}

} // namespace pcbir::stackup
