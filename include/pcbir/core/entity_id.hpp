// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_CORE_ENTITY_ID_HPP
#define PCBIR_CORE_ENTITY_ID_HPP

#include <cstdint>

namespace pcbir::core {

// Stable identity for a logical entity, assigned once at creation and
// carried across every future snapshot/commit for that entity's lifetime --
// distinct from Handle<Tag>, which is an arena-slot reference (fast,
// generation-checked, but not meant to be serialized or compared across
// unrelated arenas). EntityId is what the serialized schema and cross-layer
// references (e.g. a via naming a stackup layer) use.
class EntityId {
public:
  using ValueType = uint64_t;

  constexpr EntityId() = default;
  constexpr explicit EntityId(ValueType value) : value_(value) {}

  [[nodiscard]] constexpr ValueType value() const { return value_; }
  [[nodiscard]] constexpr bool is_null() const { return value_ == 0; }

  friend constexpr bool operator==(const EntityId&, const EntityId&) = default;
  friend constexpr bool operator<(const EntityId& lhs, const EntityId& rhs) {
    return lhs.value_ < rhs.value_;
  }

private:
  ValueType value_ = 0; // 0 is reserved for "null".
};

} // namespace pcbir::core

#endif // PCBIR_CORE_ENTITY_ID_HPP
