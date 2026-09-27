// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import_nets.hpp"

#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/kicad/sexpr.hpp"

#include <set>
#include <string>
#include <utility>

namespace pcbir::kicad {

namespace {

// Recurses into every list node looking for a `(net "NAME")` entry,
// regardless of where it's nested (a pad inside a footprint, a top-level
// segment/arc/via, a zone, ...) -- this is deliberately traversal-order-
// and depth-agnostic, unlike the other importer pieces, which only look at
// specific known sections. Recursion depth is bounded by parse_sexpr's own
// MAX_NESTING_DEPTH, the same "bounded, so safe" reasoning as
// geometry/bezier.cpp's subdivide() and SExpr::operator==.
// NOLINTNEXTLINE(misc-no-recursion)
void collect_net_names(const SExpr& node, std::set<std::string>& names) {
  if (!node.is_list()) {
    return;
  }
  if (node.children.size() >= 2 && node.children.front().is_symbol() &&
      node.children.front().text == "net" && node.children.at(1).is_string() &&
      !node.children.at(1).text.empty()) {
    names.insert(node.children.at(1).text);
  }
  for (const SExpr& child : node.children) {
    collect_net_names(child, names);
  }
}

} // namespace

connectivity::ConnectivitySnapshot import_nets(const SExpr& kicad_pcb) {
  std::set<std::string> names;
  collect_net_names(kicad_pcb, names);

  connectivity::ConnectivityWorkspace workspace;
  for (std::string name : names) {
    workspace.insert(connectivity::Net{.name = std::move(name)});
  }
  return workspace.commit();
}

} // namespace pcbir::kicad
