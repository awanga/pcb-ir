// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_CHECKED_ARITH_HPP
#define PCBIR_GEOMETRY_CHECKED_ARITH_HPP

#include <cstdint>
#include <limits>

namespace pcbir::geometry {

// Portable (no compiler-specific builtins, so behavior is identical on
// GCC/Clang/MSVC) overflow-checked int64_t arithmetic. Coordinate primitives
// in this module route through these instead of raw +/-/* so an overflow is
// caught as a checked condition rather than triggering signed-integer-
// overflow undefined behavior. Returns false (and leaves `out` unwritten) on
// overflow.

[[nodiscard]] constexpr bool checked_add(int64_t a, int64_t b, int64_t& out) {
  constexpr int64_t max = std::numeric_limits<int64_t>::max();
  constexpr int64_t min = std::numeric_limits<int64_t>::min();
  if ((b > 0 && a > max - b) || (b < 0 && a < min - b)) {
    return false;
  }
  out = a + b;
  return true;
}

[[nodiscard]] constexpr bool checked_sub(int64_t a, int64_t b, int64_t& out) {
  constexpr int64_t max = std::numeric_limits<int64_t>::max();
  constexpr int64_t min = std::numeric_limits<int64_t>::min();
  if ((b < 0 && a > max + b) || (b > 0 && a < min + b)) {
    return false;
  }
  out = a - b;
  return true;
}

[[nodiscard]] constexpr bool checked_mul(int64_t a, int64_t b, int64_t& out) {
  constexpr int64_t max = std::numeric_limits<int64_t>::max();
  constexpr int64_t min = std::numeric_limits<int64_t>::min();
  if (a == 0 || b == 0) {
    out = 0;
    return true;
  }
  // -1 is handled separately: negating `min` itself overflows, and the
  // division-based bounds checks below assume neither operand is -1.
  if (a == -1 || b == -1) {
    const int64_t other = (a == -1) ? b : a;
    if (other == min) {
      return false;
    }
    out = -other;
    return true;
  }
  if (a > 0) {
    if (b > 0) {
      if (a > max / b) {
        return false;
      }
    } else if (b < min / a) {
      return false;
    }
  } else {
    if (b > 0) {
      if (a < min / b) {
        return false;
      }
    } else if (a < max / b) {
      return false;
    }
  }
  out = a * b;
  return true;
}

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_CHECKED_ARITH_HPP
