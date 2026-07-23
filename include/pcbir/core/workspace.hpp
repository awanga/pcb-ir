// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CORE_WORKSPACE_HPP
#define PCBIR_CORE_WORKSPACE_HPP

#include "pcbir/core/arena.hpp"
#include "pcbir/core/detail/cow_table.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/core/handle.hpp"
#include "pcbir/core/snapshot.hpp"
#include "pcbir/core/snapshot_version.hpp"

#include <tuple>
#include <utility>

namespace pcbir::core {

// Mutable transactional overlay over a Snapshot<Components...>. Edits
// accumulate here; commit() produces a new immutable Snapshot without
// modifying the snapshot the workspace was opened from (docs/architecture.md
// -- Snapshots and workspaces).
template <typename... Components> class Workspace {
public:
  Workspace() = default;

  explicit Workspace(const Snapshot<Components...>& base)
      : version_(base.version()), next_id_(base.next_entity_id()), tables_(base.tables_) {}

  template <typename T> Handle<T> insert(T value) {
    const EntityId id = allocate_id();
    return std::get<detail::CowTable<T>>(tables_).write().insert(std::move(value), id);
  }

  template <typename T> void erase(Handle<T> handle) {
    std::get<detail::CowTable<T>>(tables_).write().erase(handle);
  }

  template <typename T> [[nodiscard]] T* try_get(Handle<T> handle) {
    return std::get<detail::CowTable<T>>(tables_).write().try_get(handle);
  }

  template <typename T> [[nodiscard]] const Arena<T>& table() const {
    return std::get<detail::CowTable<T>>(tables_).read();
  }

  // Produces a new immutable snapshot reflecting every edit made so far, and
  // advances this workspace to build on top of it, so further edits plus a
  // second commit() form the next transaction (git-commit style).
  [[nodiscard]] Snapshot<Components...> commit() {
    version_ = version_.next();
    return Snapshot<Components...>(version_, next_id_, tables_);
  }

private:
  [[nodiscard]] EntityId allocate_id() {
    EntityId id = next_id_;
    next_id_ = EntityId{next_id_.value() + 1};
    return id;
  }

  SnapshotVersion version_;
  EntityId next_id_{1}; // 0 is reserved for "null".
  std::tuple<detail::CowTable<Components>...> tables_;
};

} // namespace pcbir::core

#endif // PCBIR_CORE_WORKSPACE_HPP
