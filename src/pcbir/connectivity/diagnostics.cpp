// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/diagnostics.hpp"

#include "pcbir/connectivity/bus.hpp"
#include "pcbir/connectivity/differential_pair.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/arena.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/track.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace pcbir::connectivity {

namespace {

template <typename T>
void collect(const ConnectivitySnapshot& snapshot, std::vector<Diagnostic>& out) {
  snapshot.template table<T>().for_each([&out](core::EntityId id, const T& entity) {
    const DiagnosticCode code = validate(entity);
    if (code != DiagnosticCode::Valid) {
      out.push_back(Diagnostic{.id = id, .code = code});
    }
  });
}

} // namespace

DiagnosticCode validate(const Net& net) {
  if (net.name.empty()) {
    return DiagnosticCode::EmptyNetName;
  }
  return DiagnosticCode::Valid;
}

DiagnosticCode validate(const Pin& pin) {
  if (pin.pad.is_null()) {
    return DiagnosticCode::InvalidPin;
  }
  if (pin.net.is_null()) {
    return DiagnosticCode::UnconnectedPin;
  }
  return DiagnosticCode::Valid;
}

DiagnosticCode validate(const DifferentialPair& pair) {
  if (pair.positive_net.is_null() || pair.negative_net.is_null() ||
      pair.positive_net == pair.negative_net) {
    return DiagnosticCode::DegenerateDiffPair;
  }
  return DiagnosticCode::Valid;
}

DiagnosticCode validate(const Bus& bus) {
  if (bus.members.empty()) {
    return DiagnosticCode::EmptyBus;
  }
  // EntityId only exposes `==`/`<` (entity_id.hpp), not the full
  // totally_ordered set std::ranges::less requires -- sort by its
  // underlying value instead of the handle type itself.
  std::vector<core::EntityId> sorted = bus.members;
  std::ranges::sort(sorted, {}, &core::EntityId::value);
  if (std::ranges::adjacent_find(sorted) != sorted.end()) {
    return DiagnosticCode::DuplicateBusMember;
  }
  return DiagnosticCode::Valid;
}

std::vector<Diagnostic> validate(const ConnectivitySnapshot& snapshot) {
  std::vector<Diagnostic> result;
  collect<Net>(snapshot, result);
  collect<Pin>(snapshot, result);
  collect<DifferentialPair>(snapshot, result);
  collect<Bus>(snapshot, result);

  const core::Arena<Net>& nets = snapshot.table<Net>();

  // Pin-level cross-entity checks: a net reference that doesn't resolve
  // within this snapshot, and a pad claimed by more than one Pin (a short
  // -- the "at most one net per pad" invariant).
  std::unordered_map<core::EntityId::ValueType, core::EntityId> pad_owner;
  snapshot.table<Pin>().for_each([&](core::EntityId id, const Pin& pin) {
    if (!pin.net.is_null() && nets.find(pin.net).is_null()) {
      result.push_back(Diagnostic{.id = id, .code = DiagnosticCode::DanglingPinNetReference});
    }
    if (!pin.pad.is_null()) {
      const auto [it, inserted] = pad_owner.try_emplace(pin.pad.value(), id);
      if (!inserted) {
        result.push_back(Diagnostic{.id = id, .code = DiagnosticCode::DuplicatePadAssignment});
      }
    }
  });

  snapshot.table<DifferentialPair>().for_each([&](core::EntityId id, const DifferentialPair& pair) {
    const bool positive_dangling =
        !pair.positive_net.is_null() && nets.find(pair.positive_net).is_null();
    const bool negative_dangling =
        !pair.negative_net.is_null() && nets.find(pair.negative_net).is_null();
    if (positive_dangling || negative_dangling) {
      result.push_back(Diagnostic{.id = id, .code = DiagnosticCode::DanglingDiffPairMember});
    }
  });

  snapshot.table<Bus>().for_each([&](core::EntityId id, const Bus& bus) {
    for (const core::EntityId& member : bus.members) {
      if (nets.find(member).is_null()) {
        result.push_back(Diagnostic{.id = id, .code = DiagnosticCode::DanglingBusMember});
        break;
      }
    }
  });

  return result;
}

std::vector<Diagnostic>
validate_geometry_net_references(const geometry::GeometrySnapshot& geometry_snapshot,
                                 const ConnectivitySnapshot& connectivity_snapshot) {
  std::vector<Diagnostic> result;
  const core::Arena<Net>& nets = connectivity_snapshot.table<Net>();

  geometry_snapshot.table<geometry::Track>().for_each([&](core::EntityId id,
                                                          const geometry::Track& track) {
    if (!track.net.is_null() && nets.find(track.net).is_null()) {
      result.push_back(Diagnostic{.id = id, .code = DiagnosticCode::DanglingGeometryNetReference});
    }
  });

  geometry_snapshot.table<geometry::CopperPour>().for_each([&](core::EntityId id,
                                                               const geometry::CopperPour& pour) {
    if (!pour.net.is_null() && nets.find(pour.net).is_null()) {
      result.push_back(Diagnostic{.id = id, .code = DiagnosticCode::DanglingGeometryNetReference});
    }
  });

  return result;
}

} // namespace pcbir::connectivity
