// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_SEXPR_BUILD_HPP
#define PCBIR_KICAD_SEXPR_BUILD_HPP

#include "pcbir/geometry/point.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"

#include <string>
#include <utility>
#include <vector>

// Internal SExpr-construction helpers shared by every exporter piece
// (export.cpp, export_footprint.cpp, ...) -- the write-side counterpart to
// sexpr_util.hpp's find_child/find_all_children read-side helpers. Not part
// of the public API, so this lives under src/.
namespace pcbir::kicad {

[[nodiscard]] inline SExpr sym(std::string text) {
  return SExpr{.kind = SExpr::Kind::Symbol, .text = std::move(text), .children = {}};
}

[[nodiscard]] inline SExpr str(std::string text) {
  return SExpr{.kind = SExpr::Kind::String, .text = std::move(text), .children = {}};
}

[[nodiscard]] inline SExpr list(std::vector<SExpr> children) {
  return SExpr{.kind = SExpr::Kind::List, .text = {}, .children = std::move(children)};
}

// A `(tag ...rest)` list node whose first child is the Symbol `tag` --
// KiCad's near-universal per-section convention (the shape
// sexpr_util.hpp's find_child/find_all_children look for).
[[nodiscard]] inline SExpr tagged(std::string tag, std::vector<SExpr> rest) {
  std::vector<SExpr> children;
  children.reserve(rest.size() + 1);
  children.push_back(sym(std::move(tag)));
  for (SExpr& child : rest) {
    children.push_back(std::move(child));
  }
  return list(std::move(children));
}

// A `(tag X Y)` coordinate-pair node (e.g. `(start 1.5 2)`, `(xy 0 0)`) --
// the exporter's inverse of coordinate_util.hpp's parse_coordinate_pair.
[[nodiscard]] inline SExpr coordinate_pair(std::string tag, const geometry::Point& point) {
  return tagged(std::move(tag), {sym(format_nm_to_mm(point.x)), sym(format_nm_to_mm(point.y))});
}

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_SEXPR_BUILD_HPP
