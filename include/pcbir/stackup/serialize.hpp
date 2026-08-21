// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_STACKUP_SERIALIZE_HPP
#define PCBIR_STACKUP_SERIALIZE_HPP

#include "pcbir/core/snapshot.hpp"
#include "pcbir/core/workspace.hpp"
#include "pcbir/stackup/impedance_profile.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/material.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace pcbir::stackup {

// The stackup layer's own snapshot/workspace, composing every typed
// stackup entity as its own component table (docs/architecture.md). This
// is composed into the full board snapshot in a later phase; standalone
// here so the stackup layer round-trips and is testable on its own
// (schemas/stackup.fbs).
using StackupSnapshot = core::Snapshot<Material, Layer, LayerStack, ImpedanceProfile>;
using StackupWorkspace = core::Workspace<Material, Layer, LayerStack, ImpedanceProfile>;

// Serializes `snapshot` to a FlatBuffers buffer per schemas/stackup.fbs. No
// FlatBuffers type appears in this signature -- the generated headers are
// an implementation detail of serialize.cpp only.
[[nodiscard]] std::vector<uint8_t> serialize(const StackupSnapshot& snapshot);

// Deserializes a buffer produced by serialize() back into a workspace with
// every entity's original EntityId preserved (via
// Workspace::insert_with_id). Returns a Workspace rather than a Snapshot
// because the only way to obtain a Snapshot is Workspace::commit()
// (docs/architecture.md); call commit() on the result to get one. The
// buffer is verified against schema corruption before any field is read
// (docs/format-spec.md -- Serialization fuzz target); a structurally
// invalid buffer throws pcbir::FormatError rather than crashing or
// reading out of bounds.
[[nodiscard]] StackupWorkspace deserialize_stackup(std::span<const uint8_t> buffer);

} // namespace pcbir::stackup

#endif // PCBIR_STACKUP_SERIALIZE_HPP
