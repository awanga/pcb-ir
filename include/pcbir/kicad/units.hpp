// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_UNITS_HPP
#define PCBIR_KICAD_UNITS_HPP

#include <cstdint>
#include <string>
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

// Parses a KiCad-authored decimal degrees string (e.g. "45", "-30.5",
// "0") into integer degrees * 1e6 -- the same fixed-point convention
// Footprint::rotation_e6 (pcbir/geometry/footprint.hpp) and
// geometry::rotate's angle_e6 parameter use. Shares parse_mm_to_nm's exact,
// floating-point-free decimal parsing (KiCad's angle fields are written
// with at most 6 fractional digits too, and 1 degree * 1e6 is the same
// scale as 1 millimetre * 1e6 nanometres) but is kept as its own named
// function since millimetres and degrees are not interchangeable units.
//
// Throws pcbir::kicad::ImportError on a malformed decimal string or a
// value that would overflow int64_t.
[[nodiscard]] int64_t parse_degrees_to_e6(std::string_view text);

// Parses a KiCad-authored dimensionless decimal ratio (e.g. a pad's
// `roundrect_rratio`, "0.25") into a fixed-point value scaled by 1e6,
// sharing parse_mm_to_nm's exact decimal-parsing arithmetic for the same
// "no floating point" reason -- a ratio applied to an already-integer
// nanometre dimension (e.g. a roundrect corner radius) should not
// introduce platform-dependent rounding differences either.
//
// Throws pcbir::kicad::ImportError on a malformed decimal string or a
// value that would overflow int64_t.
[[nodiscard]] int64_t parse_ratio_to_e6(std::string_view text);

// Renders integer nanometres as a KiCad-style decimal millimetre string
// (the exporter's inverse of parse_mm_to_nm): exact, since 1 mm is exactly
// 1e6 nm, so no rounding is ever needed going this direction. Trailing
// fractional zeros are trimmed (e.g. 1'600'000 -> "1.6", not "1.600000"),
// matching how real pcbnew-authored files write coordinates; a whole
// value renders with no decimal point at all (e.g. 5'000'000 -> "5").
// parse_mm_to_nm(format_nm_to_mm(x)) == x for every representable x.
[[nodiscard]] std::string format_nm_to_mm(int64_t nm);

// The exporter's inverse of parse_degrees_to_e6, sharing format_nm_to_mm's
// exact (no floating point) fixed-point-to-decimal conversion -- degrees
// and millimetres share the same *1e6 scale, but are kept as separate
// named functions for the same reason their parse_ counterparts are.
[[nodiscard]] std::string format_e6_to_degrees(int64_t angle_e6);

// The exporter's inverse of parse_ratio_to_e6.
[[nodiscard]] std::string format_e6_to_ratio(int64_t ratio_e6);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_UNITS_HPP
