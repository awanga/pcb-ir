// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_C_ABI_HANDLE_UTIL_HPP
#define PCBIR_C_ABI_HANDLE_UTIL_HPP

#include "pcbir/core/arena.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/core/handle.hpp"

#include <cstddef>
#include <optional>

namespace pcbir::c_abi {

// Recovers the Handle<T> for the `index`-th live entity in `arena`'s
// canonical iteration order (Arena::for_each). Arena exposes no direct
// positional accessor, so this walks the entities and re-resolves the
// target one's EntityId via Arena::find() -- correct regardless of
// whether a slot's internal index happens to match its iteration
// position, rather than assuming it does.
template <typename T>
std::optional<core::Handle<T>> handle_at(const core::Arena<T>& arena, size_t index) {
  std::optional<core::EntityId> found_id;
  size_t position = 0;
  arena.for_each([&](core::EntityId id, const T&) {
    if (position == index) {
      found_id = id;
    }
    ++position;
  });
  if (!found_id.has_value()) {
    return std::nullopt;
  }
  const core::Handle<T> handle = arena.find(*found_id);
  if (handle.is_null()) {
    return std::nullopt;
  }
  return handle;
}

} // namespace pcbir::c_abi

#endif // PCBIR_C_ABI_HANDLE_UTIL_HPP
