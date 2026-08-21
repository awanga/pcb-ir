// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_SERIALIZE_HPP
#define PCBIR_SERIALIZE_HPP

#include "pcbir/board_snapshot.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace pcbir {

// Serializes `board` to a FlatBuffers buffer per schemas/snapshot.fbs. No
// FlatBuffers type appears in this signature -- the generated headers are
// an implementation detail of serialize.cpp only.
[[nodiscard]] std::vector<uint8_t> serialize(const BoardSnapshot& board);

// Deserializes a buffer produced by serialize() back into a BoardSnapshot,
// with every entity's original EntityId preserved within its layer. The
// buffer is verified against schema corruption before any field is read
// (docs/format-spec.md -- Versioning policy); a structurally invalid
// buffer, or one whose format_version carries an unsupported major
// version, throws FormatError rather than crashing or silently accepting
// bad data.
[[nodiscard]] BoardSnapshot deserialize_board(std::span<const uint8_t> buffer);

} // namespace pcbir

#endif // PCBIR_SERIALIZE_HPP
