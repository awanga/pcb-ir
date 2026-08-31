// SPDX-License-Identifier: Apache-2.0
#include "pcbir/c_abi/error.hpp"
#include "pcbir/c_abi/handle_util.hpp"
#include "pcbir/c_abi/types.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/core/handle.hpp"
#include "pcbir/geometry/diagnostics.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/pcbir.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using pcbir::core::EntityId;
using pcbir::core::Handle;
using pcbir::geometry::Via;

Handle<Via> to_cpp_handle(pcbir_via_handle_t handle) {
  return Handle<Via>{handle.index, handle.generation};
}

pcbir_via_handle_t to_c_handle(Handle<Via> handle) {
  return {.index = handle.index(), .generation = handle.generation()};
}

const Via* resolve_via(const pcbir_geometry_snapshot_t* snapshot, pcbir_via_handle_t handle) {
  return snapshot->snapshot.table<Via>().try_get(to_cpp_handle(handle));
}

// Every diagnostic code the project ships is defined once, in
// pcbir::geometry::DiagnosticCode; this keeps the C mirror honest without
// hand-copying the mapping at every call site.
pcbir_geometry_diagnostic_code_t to_c_diagnostic_code(pcbir::geometry::DiagnosticCode code) {
  static_assert(static_cast<int>(pcbir::geometry::DiagnosticCode::NonPositiveAnnularRing) ==
                    PCBIR_GEOMETRY_DIAGNOSTIC_NON_POSITIVE_ANNULAR_RING,
                "pcbir_geometry_diagnostic_code_t has drifted from "
                "pcbir::geometry::DiagnosticCode");
  return static_cast<pcbir_geometry_diagnostic_code_t>(code);
}

} // namespace

pcbir_status_t pcbir_geometry_via_count(const pcbir_geometry_snapshot_t* snapshot,
                                        size_t* out_count) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_count == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_count = snapshot->snapshot.table<Via>().size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_at(const pcbir_geometry_snapshot_t* snapshot,
                                     size_t index,
                                     pcbir_via_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const auto handle = pcbir::c_abi::handle_at(snapshot->snapshot.table<Via>(), index);
    if (!handle.has_value()) {
      pcbir::c_abi::set_last_error("via index out of range");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(*handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_find_by_entity_id(const pcbir_geometry_snapshot_t* snapshot,
                                                    uint64_t entity_id,
                                                    pcbir_via_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Handle<Via> handle = snapshot->snapshot.table<Via>().find(EntityId{entity_id});
    if (handle.is_null()) {
      pcbir::c_abi::set_last_error("no via with that entity id");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_entity_id(const pcbir_geometry_snapshot_t* snapshot,
                                            pcbir_via_handle_t handle,
                                            uint64_t* out_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_entity_id = snapshot->snapshot.table<Via>().id_of(to_cpp_handle(handle)).value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_position(const pcbir_geometry_snapshot_t* snapshot,
                                           pcbir_via_handle_t handle,
                                           int64_t* out_x,
                                           int64_t* out_y) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_x == nullptr || out_y == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Via* via = resolve_via(snapshot, handle);
    if (via == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid via handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_x = via->position.x;
    *out_y = via->position.y;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_drill_diameter_nm(const pcbir_geometry_snapshot_t* snapshot,
                                                    pcbir_via_handle_t handle,
                                                    int64_t* out_value) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_value == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Via* via = resolve_via(snapshot, handle);
    if (via == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid via handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_value = via->drill_diameter_nm;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_finished_hole_diameter_nm(
    const pcbir_geometry_snapshot_t* snapshot, pcbir_via_handle_t handle, int64_t* out_value) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_value == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Via* via = resolve_via(snapshot, handle);
    if (via == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid via handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_value = via->finished_hole_diameter_nm;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_pad_diameter_nm(const pcbir_geometry_snapshot_t* snapshot,
                                                  pcbir_via_handle_t handle,
                                                  int64_t* out_value) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_value == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Via* via = resolve_via(snapshot, handle);
    if (via == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid via handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_value = via->pad_diameter_nm;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_annular_ring_nm(const pcbir_geometry_snapshot_t* snapshot,
                                                  pcbir_via_handle_t handle,
                                                  int64_t* out_value) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_value == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Via* via = resolve_via(snapshot, handle);
    if (via == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid via handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_value = via->annular_ring_nm();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_start_layer_entity_id(const pcbir_geometry_snapshot_t* snapshot,
                                                        pcbir_via_handle_t handle,
                                                        uint64_t* out_layer_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_layer_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Via* via = resolve_via(snapshot, handle);
    if (via == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid via handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_layer_entity_id = via->start_layer.value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_via_end_layer_entity_id(const pcbir_geometry_snapshot_t* snapshot,
                                                      pcbir_via_handle_t handle,
                                                      uint64_t* out_layer_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_layer_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Via* via = resolve_via(snapshot, handle);
    if (via == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid via handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_layer_entity_id = via->end_layer.value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_geometry_validate(const pcbir_geometry_snapshot_t* snapshot,
                                       pcbir_geometry_diagnostics_t** out_diagnostics) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_diagnostics == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_diagnostics =
        new pcbir_geometry_diagnostics{pcbir::geometry::validate(snapshot->snapshot)};
    return PCBIR_OK;
  });
}

void pcbir_geometry_diagnostics_free(pcbir_geometry_diagnostics_t* diagnostics) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete diagnostics;
}

pcbir_status_t pcbir_geometry_diagnostics_count(const pcbir_geometry_diagnostics_t* diagnostics,
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

pcbir_status_t pcbir_geometry_diagnostics_at(const pcbir_geometry_diagnostics_t* diagnostics,
                                             size_t index,
                                             uint64_t* out_entity_id,
                                             pcbir_geometry_diagnostic_code_t* out_code) {
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
    const pcbir::geometry::Diagnostic& diagnostic = diagnostics->diagnostics[index];
    *out_entity_id = diagnostic.id.value();
    *out_code = to_c_diagnostic_code(diagnostic.code);
    return PCBIR_OK;
  });
}
