// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CORE_SNAPSHOT_VERSION_HPP
#define PCBIR_CORE_SNAPSHOT_VERSION_HPP

#include <cstdint>

namespace pcbir::core {

// Monotonically increasing generation counter for a Snapshot lineage: 0 for
// a from-scratch (never-committed) workspace, incremented by one on every
// Workspace::commit(). Distinct from a per-entity EntityId.
class SnapshotVersion {
public:
  using ValueType = uint64_t;

  constexpr SnapshotVersion() = default;
  constexpr explicit SnapshotVersion(ValueType value) : value_(value) {}

  [[nodiscard]] constexpr ValueType value() const { return value_; }
  [[nodiscard]] constexpr SnapshotVersion next() const { return SnapshotVersion{value_ + 1}; }

  friend constexpr bool operator==(const SnapshotVersion&, const SnapshotVersion&) = default;

private:
  ValueType value_ = 0;
};

} // namespace pcbir::core

#endif // PCBIR_CORE_SNAPSHOT_VERSION_HPP
