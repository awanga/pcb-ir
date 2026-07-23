// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CORE_SNAPSHOT_HPP
#define PCBIR_CORE_SNAPSHOT_HPP

#include "pcbir/core/arena.hpp"
#include "pcbir/core/detail/cow_table.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/core/snapshot_version.hpp"

#include <tuple>
#include <utility>

namespace pcbir::core {

template <typename... Components> class Workspace;

// Immutable, read-only view over one component table per type in
// `Components...`. Snapshot exposes no mutation API -- edits happen only
// through a Workspace<Components...>, whose commit() produces a new
// Snapshot (docs/architecture.md -- Snapshots and workspaces).
template <typename... Components> class Snapshot {
public:
  Snapshot() = default;

  [[nodiscard]] SnapshotVersion version() const { return version_; }
  [[nodiscard]] EntityId next_entity_id() const { return next_entity_id_; }

  template <typename T> [[nodiscard]] const Arena<T>& table() const {
    return std::get<detail::CowTable<T>>(tables_).read();
  }

private:
  friend class Workspace<Components...>;

  Snapshot(SnapshotVersion version,
           EntityId next_entity_id,
           std::tuple<detail::CowTable<Components>...> tables)
      : version_(version), next_entity_id_(next_entity_id), tables_(std::move(tables)) {}

  SnapshotVersion version_;
  EntityId next_entity_id_;
  std::tuple<detail::CowTable<Components>...> tables_;
};

} // namespace pcbir::core

#endif // PCBIR_CORE_SNAPSHOT_HPP
