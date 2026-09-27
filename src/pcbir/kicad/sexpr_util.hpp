// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_SEXPR_UTIL_HPP
#define PCBIR_KICAD_SEXPR_UTIL_HPP

#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Internal helpers shared by every importer piece (import.cpp,
// import_geometry.cpp, ...) -- not part of the public API
// (pcbir/kicad/import.hpp, pcbir/kicad/import_geometry.hpp), so this lives
// under src/, mirroring pcbir/c_abi/handle_util.hpp's precedent for a
// header shared only among sibling .cpp files.
namespace pcbir::kicad {

// Finds the first direct child of `list` that is itself a list whose own
// first child is the Symbol `tag` -- KiCad's `(tag ...)` convention for
// every named section (`(layers ...)`, `(setup ...)`, `(layer "F.Cu")`,
// a `(footprint ...)`'s own `(at ...)`, ...).
[[nodiscard]] inline const SExpr* find_child(const SExpr& list, std::string_view tag) {
  for (const SExpr& child : list.children) {
    if (child.is_list() && !child.children.empty() && child.children.front().is_symbol() &&
        child.children.front().text == tag) {
      return &child;
    }
  }
  return nullptr;
}

// find_child()'s multi-result cousin, for sections KiCad repeats
// (`(gr_line ...)`, `(gr_arc ...)`, `(footprint ...)`, `(pad ...)`, ...).
[[nodiscard]] inline std::vector<const SExpr*> find_all_children(const SExpr& list,
                                                                 std::string_view tag) {
  std::vector<const SExpr*> result;
  for (const SExpr& child : list.children) {
    if (child.is_list() && !child.children.empty() && child.children.front().is_symbol() &&
        child.children.front().text == tag) {
      result.push_back(&child);
    }
  }
  return result;
}

[[nodiscard]] inline int64_t parse_i64(const SExpr& symbol) {
  const std::string& text = symbol.text;
  int64_t value = 0;
  // std::from_chars's interface is a [begin, end) pointer pair; there is
  // no pointer-arithmetic-free way to name "one past the last character"
  // of a std::string.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(text.data(), end, value);
  if (ec != std::errc{} || ptr != end) {
    throw ImportError("expected an integer, got '" + text + "'");
  }
  return value;
}

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_SEXPR_UTIL_HPP
