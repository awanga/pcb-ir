// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CORE_HANDLE_HPP
#define PCBIR_CORE_HANDLE_HPP

#include <cstdint>

namespace pcbir::core {

// Opaque, generation-checked reference to a T stored in an Arena<T, Tag>.
// `Tag` (defaults to T) is a phantom type so Handle<Pad> and Handle<Via> are
// distinct types at compile time, without inheritance. Trivially copyable
// and comparable; a default-constructed Handle is null.
template <typename Tag> class Handle {
public:
  using Index = uint32_t;
  using Generation = uint32_t;

  constexpr Handle() = default;
  // index/generation are both plain uint32_t; a distinct wrapper type per
  // argument would be overkill for this small, rarely-hand-constructed type.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  constexpr Handle(Index index, Generation generation) : index_(index), generation_(generation) {}

  [[nodiscard]] constexpr bool is_null() const { return generation_ == 0; }
  [[nodiscard]] constexpr Index index() const { return index_; }
  [[nodiscard]] constexpr Generation generation() const { return generation_; }

  friend constexpr bool operator==(const Handle&, const Handle&) = default;

private:
  Index index_ = 0;
  Generation generation_ = 0; // 0 is reserved for "null"; live slots start at 1.
};

} // namespace pcbir::core

#endif // PCBIR_CORE_HANDLE_HPP
