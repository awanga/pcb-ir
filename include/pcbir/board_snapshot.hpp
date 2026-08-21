// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_BOARD_SNAPSHOT_HPP
#define PCBIR_BOARD_SNAPSHOT_HPP

#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/extension.hpp"
#include "pcbir/format_version.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/passthrough_blob.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <vector>

namespace pcbir {

// The full-board snapshot: one immutable snapshot per layer, each already
// independently round-trippable, plus the cross-cutting wire-format
// version, typed extensions, and opaque passthrough blobs. Composing a
// board is nothing more than grouping these already-existing pieces
// together -- see serialize()/deserialize_board() (pcbir/serialize.hpp)
// for how the group is written to, and read back from, one buffer
// (schemas/snapshot.fbs).
struct BoardSnapshot {
  FormatVersion format_version = CURRENT_FORMAT_VERSION;
  geometry::GeometrySnapshot geometry;
  connectivity::ConnectivitySnapshot connectivity;
  stackup::StackupSnapshot stackup;
  std::vector<Extension> extensions;
  std::vector<PassthroughBlob> passthrough_blobs;
};

} // namespace pcbir

#endif // PCBIR_BOARD_SNAPSHOT_HPP
