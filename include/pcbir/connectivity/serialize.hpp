// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CONNECTIVITY_SERIALIZE_HPP
#define PCBIR_CONNECTIVITY_SERIALIZE_HPP

#include "pcbir/connectivity/bus.hpp"
#include "pcbir/connectivity/differential_pair.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/core/snapshot.hpp"
#include "pcbir/core/workspace.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace pcbir::connectivity {

// The connectivity layer's own snapshot/workspace, composing every
// connectivity entity as its own component table (docs/architecture.md).
// This is composed into the full board snapshot in a later phase;
// standalone here so the connectivity layer round-trips and is testable on
// its own, mirroring pcbir::geometry::GeometrySnapshot.
using ConnectivitySnapshot = core::Snapshot<Net, Pin, DifferentialPair, Bus>;
using ConnectivityWorkspace = core::Workspace<Net, Pin, DifferentialPair, Bus>;

// Serializes `snapshot` to a FlatBuffers buffer per schemas/connectivity.fbs.
// No FlatBuffers type appears in this signature -- the generated headers
// are an implementation detail of serialize.cpp only.
[[nodiscard]] std::vector<uint8_t> serialize(const ConnectivitySnapshot& snapshot);

// Deserializes a buffer produced by serialize() back into a workspace with
// every entity's original EntityId preserved (via
// Workspace::insert_with_id). Returns a Workspace rather than a Snapshot
// because the only way to obtain a Snapshot is Workspace::commit()
// (docs/architecture.md); call commit() on the result to get one.
[[nodiscard]] ConnectivityWorkspace deserialize_connectivity(std::span<const uint8_t> buffer);

} // namespace pcbir::connectivity

#endif // PCBIR_CONNECTIVITY_SERIALIZE_HPP
