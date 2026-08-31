// SPDX-License-Identifier: Apache-2.0
#include "pcbir/c_abi/error.hpp"
#include "pcbir/c_abi/handle_util.hpp"
#include "pcbir/c_abi/types.hpp"
#include "pcbir/connectivity/diagnostics.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/core/handle.hpp"
#include "pcbir/pcbir.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::core::EntityId;
using pcbir::core::Handle;

Handle<Net> to_cpp_handle(pcbir_net_handle_t handle) {
  return Handle<Net>{handle.index, handle.generation};
}

pcbir_net_handle_t to_c_handle(Handle<Net> handle) {
  return {.index = handle.index(), .generation = handle.generation()};
}

Handle<Pin> to_cpp_handle(pcbir_pin_handle_t handle) {
  return Handle<Pin>{handle.index, handle.generation};
}

pcbir_pin_handle_t to_c_handle(Handle<Pin> handle) {
  return {.index = handle.index(), .generation = handle.generation()};
}

const Net* resolve_net(const pcbir_connectivity_snapshot_t* snapshot, pcbir_net_handle_t handle) {
  return snapshot->snapshot.table<Net>().try_get(to_cpp_handle(handle));
}

const Pin* resolve_pin(const pcbir_connectivity_snapshot_t* snapshot, pcbir_pin_handle_t handle) {
  return snapshot->snapshot.table<Pin>().try_get(to_cpp_handle(handle));
}

pcbir_connectivity_diagnostic_code_t
to_c_diagnostic_code(pcbir::connectivity::DiagnosticCode code) {
  static_assert(static_cast<int>(pcbir::connectivity::DiagnosticCode::DanglingBusMember) ==
                    PCBIR_CONNECTIVITY_DIAGNOSTIC_DANGLING_BUS_MEMBER,
                "pcbir_connectivity_diagnostic_code_t has drifted from "
                "pcbir::connectivity::DiagnosticCode");
  return static_cast<pcbir_connectivity_diagnostic_code_t>(code);
}

} // namespace

pcbir_status_t pcbir_connectivity_net_count(const pcbir_connectivity_snapshot_t* snapshot,
                                            size_t* out_count) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_count == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_count = snapshot->snapshot.table<Net>().size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_net_at(const pcbir_connectivity_snapshot_t* snapshot,
                                         size_t index,
                                         pcbir_net_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const auto handle = pcbir::c_abi::handle_at(snapshot->snapshot.table<Net>(), index);
    if (!handle.has_value()) {
      pcbir::c_abi::set_last_error("net index out of range");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(*handle);
    return PCBIR_OK;
  });
}

pcbir_status_t
pcbir_connectivity_net_find_by_entity_id(const pcbir_connectivity_snapshot_t* snapshot,
                                         uint64_t entity_id,
                                         pcbir_net_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Handle<Net> handle = snapshot->snapshot.table<Net>().find(EntityId{entity_id});
    if (handle.is_null()) {
      pcbir::c_abi::set_last_error("no net with that entity id");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_net_entity_id(const pcbir_connectivity_snapshot_t* snapshot,
                                                pcbir_net_handle_t handle,
                                                uint64_t* out_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_entity_id = snapshot->snapshot.table<Net>().id_of(to_cpp_handle(handle)).value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_net_name(const pcbir_connectivity_snapshot_t* snapshot,
                                           pcbir_net_handle_t handle,
                                           const char** out_name,
                                           size_t* out_length) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_name == nullptr || out_length == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Net* net = resolve_net(snapshot, handle);
    if (net == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid net handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_name = net->name.c_str();
    *out_length = net->name.size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_pin_count(const pcbir_connectivity_snapshot_t* snapshot,
                                            size_t* out_count) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_count == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_count = snapshot->snapshot.table<Pin>().size();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_pin_at(const pcbir_connectivity_snapshot_t* snapshot,
                                         size_t index,
                                         pcbir_pin_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const auto handle = pcbir::c_abi::handle_at(snapshot->snapshot.table<Pin>(), index);
    if (!handle.has_value()) {
      pcbir::c_abi::set_last_error("pin index out of range");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(*handle);
    return PCBIR_OK;
  });
}

pcbir_status_t
pcbir_connectivity_pin_find_by_entity_id(const pcbir_connectivity_snapshot_t* snapshot,
                                         uint64_t entity_id,
                                         pcbir_pin_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_handle == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Handle<Pin> handle = snapshot->snapshot.table<Pin>().find(EntityId{entity_id});
    if (handle.is_null()) {
      pcbir::c_abi::set_last_error("no pin with that entity id");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_handle = to_c_handle(handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_pin_entity_id(const pcbir_connectivity_snapshot_t* snapshot,
                                                pcbir_pin_handle_t handle,
                                                uint64_t* out_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_entity_id = snapshot->snapshot.table<Pin>().id_of(to_cpp_handle(handle)).value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_pin_pad_entity_id(const pcbir_connectivity_snapshot_t* snapshot,
                                                    pcbir_pin_handle_t handle,
                                                    uint64_t* out_pad_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_pad_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Pin* pin = resolve_pin(snapshot, handle);
    if (pin == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid pin handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_pad_entity_id = pin->pad.value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_pin_net_entity_id(const pcbir_connectivity_snapshot_t* snapshot,
                                                    pcbir_pin_handle_t handle,
                                                    uint64_t* out_net_entity_id) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_net_entity_id == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const Pin* pin = resolve_pin(snapshot, handle);
    if (pin == nullptr) {
      pcbir::c_abi::set_last_error("stale or invalid pin handle");
      return PCBIR_ERROR_NOT_FOUND;
    }
    *out_net_entity_id = pin->net.value();
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_validate(const pcbir_connectivity_snapshot_t* snapshot,
                                           pcbir_connectivity_diagnostics_t** out_diagnostics) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_diagnostics == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_diagnostics =
        new pcbir_connectivity_diagnostics{pcbir::connectivity::validate(snapshot->snapshot)};
    return PCBIR_OK;
  });
}

void pcbir_connectivity_diagnostics_free(pcbir_connectivity_diagnostics_t* diagnostics) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete diagnostics;
}

pcbir_status_t
pcbir_connectivity_diagnostics_count(const pcbir_connectivity_diagnostics_t* diagnostics,
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

pcbir_status_t
pcbir_connectivity_diagnostics_at(const pcbir_connectivity_diagnostics_t* diagnostics,
                                  size_t index,
                                  uint64_t* out_entity_id,
                                  pcbir_connectivity_diagnostic_code_t* out_code) {
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
    const pcbir::connectivity::Diagnostic& diagnostic = diagnostics->diagnostics[index];
    *out_entity_id = diagnostic.id.value();
    *out_code = to_c_diagnostic_code(diagnostic.code);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_workspace_create(pcbir_connectivity_workspace_t** out_workspace) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (out_workspace == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_workspace = new pcbir_connectivity_workspace{};
    return PCBIR_OK;
  });
}

pcbir_status_t
pcbir_connectivity_workspace_create_from_snapshot(const pcbir_connectivity_snapshot_t* snapshot,
                                                  pcbir_connectivity_workspace_t** out_workspace) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (snapshot == nullptr || out_workspace == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_workspace = new pcbir_connectivity_workspace{
        pcbir::connectivity::ConnectivityWorkspace(snapshot->snapshot)};
    return PCBIR_OK;
  });
}

void pcbir_connectivity_workspace_free(pcbir_connectivity_workspace_t* workspace) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete workspace;
}

pcbir_status_t pcbir_connectivity_workspace_insert_net(pcbir_connectivity_workspace_t* workspace,
                                                       const char* name,
                                                       size_t name_length,
                                                       pcbir_net_handle_t* out_handle) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (workspace == nullptr || out_handle == nullptr || (name == nullptr && name_length != 0)) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const std::string net_name = name != nullptr ? std::string(name, name_length) : std::string();
    const Handle<Net> handle = workspace->workspace.insert(Net{.name = net_name});
    *out_handle = to_c_handle(handle);
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_connectivity_workspace_commit(pcbir_connectivity_workspace_t* workspace,
                                                   pcbir_connectivity_snapshot_t** out_snapshot) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (workspace == nullptr || out_snapshot == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_snapshot = new pcbir_connectivity_snapshot{workspace->workspace.commit()};
    return PCBIR_OK;
  });
}
