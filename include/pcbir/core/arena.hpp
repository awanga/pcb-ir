// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CORE_ARENA_HPP
#define PCBIR_CORE_ARENA_HPP

#include "pcbir/core/entity_id.hpp"
#include "pcbir/core/handle.hpp"

#include <cassert>
#include <cstddef>
#include <deque>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pcbir::core {

// Bulk-allocated, generation-checked storage for snapshot-owned objects of
// type T. Backed by std::deque, so slots are allocated in chunks (no
// per-object heap churn) and references to existing slots stay valid across
// further insertions. A handle returned by insert() becomes detectably
// stale after erase(): the slot's generation counter is bumped, so a
// use-after-erase resolves to nullptr (release) or trips an assert (debug),
// even if the slot index has since been recycled for a new entity.
//
// for_each() visits live entities in slot order, which is insertion order
// for slots that have never been erased and reused -- the canonical,
// stable iteration order the determinism invariants depend on
// (docs/architecture.md).
template <typename T, typename Tag = T> class Arena {
public:
  using HandleType = Handle<Tag>;

  Arena() = default;

  // Inserts `value`, assigning it `id` as its stable entity identity.
  // Returns a handle valid until a matching erase().
  HandleType insert(T value, EntityId id) {
    // `typename` is required here (HandleType::Index depends on the class
    // template parameter Tag); some clang-tidy versions misreport it as
    // redundant, but GCC and the standard both require it.
    // NOLINTNEXTLINE(readability-redundant-typename)
    typename HandleType::Index index;
    if (!free_list_.empty()) {
      index = free_list_.back();
      free_list_.pop_back();
    } else {
      // NOLINTNEXTLINE(readability-redundant-typename)
      index = static_cast<typename HandleType::Index>(slots_.size());
      slots_.emplace_back();
    }

    // `index` is either a previously-issued (and thus in-range) free-list
    // entry, or slots_.size() taken right before the emplace_back() above.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    Slot& slot = slots_[index];
    slot.value.emplace(std::move(value));
    slot.id = id;
    if (slot.generation == 0) {
      slot.generation = 1; // Generation 0 is reserved for "null".
    }
    id_index_[id.value()] = index;
    return HandleType{index, slot.generation};
  }

  void erase(HandleType handle) {
    Slot* slot = locate(handle);
    assert(slot != nullptr && "erase() called with a stale or invalid handle");
    if (slot == nullptr) {
      return;
    }
    id_index_.erase(slot->id.value());
    slot->value.reset();
    slot->generation++; // Invalidates every outstanding copy of `handle`.
    free_list_.push_back(handle.index());
  }

  [[nodiscard]] T* try_get(HandleType handle) {
    Slot* slot = locate(handle);
    // locate() only ever returns a slot for which value.has_value() is true.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return slot != nullptr ? &*slot->value : nullptr;
  }

  [[nodiscard]] const T* try_get(HandleType handle) const {
    const Slot* slot = locate(handle);
    // locate() only ever returns a slot for which value.has_value() is true.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return slot != nullptr ? &*slot->value : nullptr;
  }

  [[nodiscard]] T& get(HandleType handle) {
    T* value = try_get(handle);
    assert(value != nullptr && "get() called with a stale or invalid handle");
    return *value;
  }

  [[nodiscard]] const T& get(HandleType handle) const {
    const T* value = try_get(handle);
    assert(value != nullptr && "get() called with a stale or invalid handle");
    return *value;
  }

  // Recovers a fresh handle for `id`, or a null handle if no live entity
  // has that id (never erased entities keep a stable id-to-handle mapping,
  // but a handle's generation can still change across an erase+reuse).
  [[nodiscard]] HandleType find(EntityId id) const {
    auto it = id_index_.find(id.value());
    if (it == id_index_.end()) {
      return HandleType{};
    }
    // `it->second` is only ever an index this Arena itself assigned.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Slot& slot = slots_[it->second];
    return HandleType{it->second, slot.generation};
  }

  [[nodiscard]] EntityId id_of(HandleType handle) const {
    const Slot* slot = locate(handle);
    return slot != nullptr ? slot->id : EntityId{};
  }

  [[nodiscard]] std::size_t size() const { return id_index_.size(); }

  // Canonical-order, read-only traversal of live entities (slot order). `fn`
  // is invoked once per live entity, so it is deliberately never forwarded
  // (forwarding an rvalue-reference callable into more than one call would
  // invoke it after a possible move-from on the first call).
  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
  template <typename Fn> void for_each(Fn&& fn) const {
    for (const Slot& slot : slots_) {
      if (slot.value.has_value()) {
        fn(slot.id, *slot.value);
      }
    }
  }

private:
  struct Slot {
    std::optional<T> value;
    EntityId id;
    // NOLINTNEXTLINE(readability-redundant-typename)
    typename HandleType::Generation generation = 0;
  };

  [[nodiscard]] const Slot* locate(HandleType handle) const {
    if (handle.is_null() || handle.index() >= slots_.size()) {
      return nullptr;
    }
    // Bounds-checked immediately above.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Slot& slot = slots_[handle.index()];
    if (slot.generation != handle.generation() || !slot.value.has_value()) {
      return nullptr;
    }
    return &slot;
  }

  [[nodiscard]] Slot* locate(HandleType handle) {
    // Safe: `this` is genuinely non-const here, so the pointer the const
    // overload returns is safe to hand back as non-const too.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<Slot*>(std::as_const(*this).locate(handle));
  }

  std::deque<Slot> slots_;
  // NOLINTNEXTLINE(readability-redundant-typename)
  std::vector<typename HandleType::Index> free_list_;
  // NOLINTNEXTLINE(readability-redundant-typename)
  std::unordered_map<EntityId::ValueType, typename HandleType::Index> id_index_;
};

} // namespace pcbir::core

#endif // PCBIR_CORE_ARENA_HPP
