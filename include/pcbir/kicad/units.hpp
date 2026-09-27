// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_UNITS_HPP
#define PCBIR_KICAD_UNITS_HPP

#include <cstdint>
#include <string_view>

namespace pcbir::kicad {

// Parses a KiCad-authored decimal string in millimetres (e.g.
// "10.707107", "-1.6", "5") into integer nanometres, exactly. KiCad's own
// internal unit is nanometres, so any coordinate it writes has at most 6
// fractional digits -- this conversion is therefore lossless, unlike a
// floating-point mm-to-nm scaling, which could round differently across
// platforms (docs/format-spec.md's "no floating point in serialized
// output" discipline). A 7th+ fractional digit, if ever present, is
// rounded to the nearest nanometre (half away from zero) using pure
// integer arithmetic -- still no floating point -- rather than truncated
// silently.
//
// Throws pcbir::kicad::ImportError (pcbir/kicad/import.hpp) on a
// malformed decimal string or a value that would overflow int64_t
// nanometres.
[[nodiscard]] int64_t parse_mm_to_nm(std::string_view text);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_UNITS_HPP
