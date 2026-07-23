// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CORE_DETAIL_COW_TABLE_HPP
#define PCBIR_CORE_DETAIL_COW_TABLE_HPP

#include "pcbir/core/arena.hpp"

#include <memory>

namespace pcbir::core::detail {

// Copy-on-write handle to one component's Arena<T>. Snapshot and Workspace
// both store a CowTable<T> per component type; committing a workspace
// copies its CowTables by value (a shared_ptr refcount bump), so a
// component table nobody edited since the last commit is shared, not
// duplicated -- the "structural sharing where possible" invariant from
// docs/architecture.md.
template <typename T, typename Tag = T> class CowTable {
public:
  CowTable() : arena_(std::make_shared<Arena<T, Tag>>()) {}

  [[nodiscard]] const Arena<T, Tag>& read() const { return *arena_; }

  // Returns a private, mutable view of the table, cloning the underlying
  // arena first if it is currently shared with another Snapshot/Workspace.
  [[nodiscard]] Arena<T, Tag>& write() {
    if (arena_.use_count() > 1) {
      arena_ = std::make_shared<Arena<T, Tag>>(*arena_);
    }
    return *arena_;
  }

private:
  std::shared_ptr<Arena<T, Tag>> arena_;
};

} // namespace pcbir::core::detail

#endif // PCBIR_CORE_DETAIL_COW_TABLE_HPP
