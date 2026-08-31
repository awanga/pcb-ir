// SPDX-License-Identifier: Apache-2.0
#include "pcbir/c_abi/error.hpp"
#include "pcbir/c_abi/handle_util.hpp"
#include "pcbir/c_abi/types.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/core/handle.hpp"
#include "pcbir/pcbir.h"
#include "pcbir/stackup/diagnostics.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/material.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using pcbir::core::EntityId;
using pcbir::core::Handle;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::Material;

Handle<Material> to_cpp_handle(pcbir_material_handle_t handle) {
  return Handle<Material>{handle.index, handle.generation};
}

pcbir_material_handle_t to_c_handle(Handle<Material> handle) {
  return {.index = handle.index(), .generation = handle.generation()};
}

Handle<Layer> to_cpp_handle(pcbir_layer_handle_t handle) {
  return Handle<Layer>{handle.index, handle.generation};
}

pcbir_layer_handle_t to_c_handle(Handle<Layer> handle) {
  return {.index = handle.index(), .generation = handle.generation()};
}

const Material* resolve_material(const pcbir_stackup_snapshot_t* snapshot,
                                 pcbir_material_handle_t handle) {
  return snapshot->snapshot.table<Material>().try_get(to_cpp_handle(handle));
}

const Layer* resolve_layer(const pcbir_stackup_snapshot_t* snapshot, pcbir_layer_handle_t handle) {
  return snapshot->snapshot.table<Layer>().try_get(to_cpp_handle(handle));
}

pcbir_layer_kind_t to_c_layer_kind(LayerKind kind) {
  static_assert(static_cast<int>(LayerKind::EdgeCuts) == PCBIR_LAYER_KIND_EDGE_CUTS,
                "pcbir_layer_kind_t has drifted from pcbir::stackup::LayerKind");
  return static_cast<pcbir_layer_kind_t>(kind);
}

pcbir_stackup_diagnostic_code_t to_c_diagnostic_code(pcbir::stackup::DiagnosticCode code) {
  static_assert(static_cast<int>(pcbir::stackup::DiagnosticCode::DanglingViaLayerReference) ==
                    PCBIR_STACKUP_DIAGNOSTIC_DANGLING_VIA_LAYER_REFERENCE,
                "pcbir_stackup_diagnostic_code_t has drifted from pcbir::stackup::DiagnosticCode");
  return static_cast<pcbir_stackup_diagnostic_code_t>(code);
}

} // namespace

pcbir_status_t pcbir_stackup_material_count(const pcbir_stackup_snapshot_t* snapshot,
                                            size_t* out_count) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_count == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_count = snapshot->snapshot.table<Material>().size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_material_at(const pcbir_stackup_snapshot_t* snapshot,
                                         size_t index,
                                         pcbir_material_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const auto handle = pcbir::c_abi::handle_at(snapshot->snapshot.table<Material>(), index);
    if (!handle.has_value()) {
      pcbir::c_abi::set_last_error("material index out of range");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(*handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_material_find_by_entity_id(const pcbir_stackup_snapshot_t* snapshot,
                                                        uint64_t entity_id,
                                                        pcbir_material_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Handle<Material> handle = snapshot->snapshot.table<Material>().find(EntityId{entity_id});
    if (handle.is_null()) {
      pcbir::c_abi::set_last_error("no material with that entity id");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_material_entity_id(const pcbir_stackup_snapshot_t* snapshot,
                                                pcbir_material_handle_t handle,
                                                uint64_t* out_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_entity_id = snapshot->snapshot.table<Material>().id_of(to_cpp_handle(handle)).value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_material_name(const pcbir_stackup_snapshot_t* snapshot,
                                           pcbir_material_handle_t handle,
                                           const char** out_name,
                                           size_t* out_length) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_name == nullptr || out_length == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Material* material = resolve_material(snapshot, handle);
    if (material == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid material handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_name = material->name.c_str();
    *out_length = material->name.size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_material_dielectric_constant_e6(
    const pcbir_stackup_snapshot_t* snapshot, pcbir_material_handle_t handle, int64_t* out_value) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_value == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Material* material = resolve_material(snapshot, handle);
    if (material == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid material handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_value = material->dielectric_constant_e6;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_material_loss_tangent_e6(const pcbir_stackup_snapshot_t* snapshot,
                                                      pcbir_material_handle_t handle,
                                                      int64_t* out_value) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_value == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Material* material = resolve_material(snapshot, handle);
    if (material == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid material handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_value = material->loss_tangent_e6;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_count(const pcbir_stackup_snapshot_t* snapshot,
                                         size_t* out_count) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_count == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_count = snapshot->snapshot.table<Layer>().size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_at(const pcbir_stackup_snapshot_t* snapshot,
                                      size_t index,
                                      pcbir_layer_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const auto handle = pcbir::c_abi::handle_at(snapshot->snapshot.table<Layer>(), index);
    if (!handle.has_value()) {
      pcbir::c_abi::set_last_error("layer index out of range");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(*handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_find_by_entity_id(const pcbir_stackup_snapshot_t* snapshot,
                                                     uint64_t entity_id,
                                                     pcbir_layer_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Handle<Layer> handle = snapshot->snapshot.table<Layer>().find(EntityId{entity_id});
    if (handle.is_null()) {
      pcbir::c_abi::set_last_error("no layer with that entity id");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_entity_id(const pcbir_stackup_snapshot_t* snapshot,
                                             pcbir_layer_handle_t handle,
                                             uint64_t* out_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_entity_id = snapshot->snapshot.table<Layer>().id_of(to_cpp_handle(handle)).value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_name(const pcbir_stackup_snapshot_t* snapshot,
                                        pcbir_layer_handle_t handle,
                                        const char** out_name,
                                        size_t* out_length) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_name == nullptr || out_length == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Layer* layer = resolve_layer(snapshot, handle);
    if (layer == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid layer handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_name = layer->name.c_str();
    *out_length = layer->name.size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_kind(const pcbir_stackup_snapshot_t* snapshot,
                                        pcbir_layer_handle_t handle,
                                        pcbir_layer_kind_t* out_kind) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_kind == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Layer* layer = resolve_layer(snapshot, handle);
    if (layer == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid layer handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_kind = to_c_layer_kind(layer->kind);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_thickness_nm(const pcbir_stackup_snapshot_t* snapshot,
                                                pcbir_layer_handle_t handle,
                                                int64_t* out_value) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_value == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Layer* layer = resolve_layer(snapshot, handle);
    if (layer == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid layer handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_value = layer->thickness_nm;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_roughness_nm(const pcbir_stackup_snapshot_t* snapshot,
                                                pcbir_layer_handle_t handle,
                                                int64_t* out_value) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_value == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Layer* layer = resolve_layer(snapshot, handle);
    if (layer == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid layer handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_value = layer->roughness_nm;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_layer_material_entity_id(const pcbir_stackup_snapshot_t* snapshot,
                                                      pcbir_layer_handle_t handle,
                                                      uint64_t* out_material_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_material_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Layer* layer = resolve_layer(snapshot, handle);
    if (layer == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid layer handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_material_entity_id = layer->material.value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_validate(const pcbir_stackup_snapshot_t* snapshot,
                                      pcbir_stackup_diagnostics_t** out_diagnostics) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_diagnostics == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_diagnostics = new pcbir_stackup_diagnostics{pcbir::stackup::validate(snapshot->snapshot)};
    return PCBIR_OK;
  });
}

void pcbir_stackup_diagnostics_free(pcbir_stackup_diagnostics_t* diagnostics) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete diagnostics;
}

pcbir_status_t pcbir_stackup_diagnostics_count(const pcbir_stackup_diagnostics_t* diagnostics,
                                               size_t* out_count) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (diagnostics == nullptr || out_count == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_count = diagnostics->diagnostics.size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_stackup_diagnostics_at(const pcbir_stackup_diagnostics_t* diagnostics,
                                            size_t index,
                                            uint64_t* out_entity_id,
                                            pcbir_stackup_diagnostic_code_t* out_code) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (diagnostics == nullptr || out_entity_id == nullptr || out_code == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    if (index >= diagnostics->diagnostics.size()) {
      pcbir::c_abi::set_last_error("diagnostic index out of range");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const pcbir::stackup::Diagnostic& diagnostic = diagnostics->diagnostics[index];
    *out_entity_id = diagnostic.id.value();
    *out_code = to_c_diagnostic_code(diagnostic.code);
    return PCBIR_OK;
  });
}
