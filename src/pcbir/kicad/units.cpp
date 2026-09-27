// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/units.hpp"

#include "pcbir/geometry/checked_arith.hpp"
#include "pcbir/kicad/import.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pcbir::kicad {

namespace {

constexpr int64_t SCALE_PER_UNIT = 1'000'000;
constexpr std::size_t FRACTION_DIGITS = 6;

[[nodiscard]] bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

[[nodiscard]] int64_t digit_value(char c) {
  return c - '0';
}

// Accumulates `digits` (as consecutive base-10 digits, most significant
// first) into `out`, checked against int64_t overflow at every step.
[[nodiscard]] bool accumulate_digits(std::string_view digits, int64_t& out) {
  for (const char c : digits) {
    int64_t scaled = 0;
    int64_t next = 0;
    if (!geometry::checked_mul(out, 10, scaled) ||
        !geometry::checked_add(scaled, digit_value(c), next)) {
      return false;
    }
    out = next;
  }
  return true;
}

// Shared exact decimal-to-fixed-point-1e6 parser behind both
// parse_mm_to_nm and parse_degrees_to_e6 (units.hpp) -- identical
// arithmetic (scale by 1e6, 6 fractional digits, round half away from
// zero), differing only in `unit_name` for error messages, since
// millimetres and degrees are still distinct units at the API level.
[[nodiscard]] int64_t parse_decimal_e6(std::string_view text, std::string_view unit_name) {
  std::size_t pos = 0;
  bool negative = false;
  if (pos < text.size() && (text.at(pos) == '-' || text.at(pos) == '+')) {
    negative = text.at(pos) == '-';
    ++pos;
  }

  const std::size_t integer_start = pos;
  while (pos < text.size() && is_digit(text.at(pos))) {
    ++pos;
  }
  const std::string_view integer_digits = text.substr(integer_start, pos - integer_start);

  std::string_view fraction_digits;
  if (pos < text.size() && text.at(pos) == '.') {
    ++pos;
    const std::size_t fraction_start = pos;
    while (pos < text.size() && is_digit(text.at(pos))) {
      ++pos;
    }
    fraction_digits = text.substr(fraction_start, pos - fraction_start);
  }

  if (pos != text.size() || (integer_digits.empty() && fraction_digits.empty())) {
    throw ImportError("expected a decimal " + std::string(unit_name) + " value, got '" +
                      std::string(text) + "'");
  }

  int64_t integer_part = 0;
  if (!accumulate_digits(integer_digits, integer_part)) {
    throw ImportError(std::string(unit_name) + " value overflows: '" + std::string(text) + "'");
  }

  // Only the first FRACTION_DIGITS (6, matching KiCad's own internal
  // precision) map directly to the fixed-point result; anything beyond
  // that is rounded into the 6th digit rather than silently dropped.
  int64_t fraction_part = 0;
  const std::size_t used = std::min(fraction_digits.size(), FRACTION_DIGITS);
  if (!accumulate_digits(fraction_digits.substr(0, used), fraction_part)) {
    throw ImportError(std::string(unit_name) + " value overflows: '" + std::string(text) + "'");
  }
  for (std::size_t i = used; i < FRACTION_DIGITS; ++i) {
    fraction_part *= 10; // Pad missing trailing digits with zero.
  }
  if (fraction_digits.size() > FRACTION_DIGITS &&
      digit_value(fraction_digits.at(FRACTION_DIGITS)) >= 5) {
    ++fraction_part; // Round half away from zero into the 6th digit.
  }

  int64_t scaled_integer = 0;
  int64_t magnitude = 0;
  if (!geometry::checked_mul(integer_part, SCALE_PER_UNIT, scaled_integer) ||
      !geometry::checked_add(scaled_integer, fraction_part, magnitude)) {
    throw ImportError(std::string(unit_name) + " value overflows: '" + std::string(text) + "'");
  }

  return negative ? -magnitude : magnitude;
}

} // namespace

int64_t parse_mm_to_nm(std::string_view text) {
  return parse_decimal_e6(text, "millimetre");
}

int64_t parse_degrees_to_e6(std::string_view text) {
  return parse_decimal_e6(text, "degree");
}

} // namespace pcbir::kicad
