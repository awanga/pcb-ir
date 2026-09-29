// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import_via.hpp"

#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/kicad/units.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <map>
#include <string>

#include "coordinate_util.hpp"
#include "sexpr_util.hpp"

namespace pcbir::kicad {

namespace {

void import_via_entry(const SExpr& via,
                      const stackup::StackupSnapshot& stackup,
                      const std::map<std::string, core::EntityId>& net_index,
                      geometry::GeometryWorkspace& geometry_workspace,
                      connectivity::ConnectivityWorkspace& connectivity_workspace) {
  const SExpr* at_node = find_child(via, "at");
  const SExpr* size_node = find_child(via, "size");
  const SExpr* drill_node = find_child(via, "drill");
  const SExpr* layers_node = find_child(via, "layers");
  if (at_node == nullptr || size_node == nullptr || size_node->children.size() < 2 ||
      drill_node == nullptr || drill_node->children.size() < 2 || layers_node == nullptr) {
    throw ImportError("malformed (via ...): missing (at/size/drill/layers ...)");
  }

  const geometry::Point position = parse_coordinate_pair(*at_node);
  const int64_t pad_diameter_nm = parse_mm_to_nm(size_node->children.at(1).text);
  // Same board-wide-design-rule approximation import_footprint.cpp's
  // thru-hole pad handling documents: a free via's (drill ...) gives only
  // the drill diameter, never a separately-tracked finished (plated) hole
  // diameter.
  const int64_t drill_diameter_nm = parse_mm_to_nm(drill_node->children.at(1).text);
  const auto [start_layer, end_layer] =
      resolve_via_layer_span(stackup, parse_layer_names(*layers_node));

  const auto handle =
      geometry_workspace.insert(geometry::Via{.position = position,
                                              .drill_diameter_nm = drill_diameter_nm,
                                              .finished_hole_diameter_nm = drill_diameter_nm,
                                              .pad_diameter_nm = pad_diameter_nm,
                                              .start_layer = start_layer,
                                              .end_layer = end_layer,
                                              .pad_number = ""});
  const core::EntityId entity_id = geometry_workspace.table<geometry::Via>().id_of(handle);
  const core::EntityId net_id = resolve_optional_net(via, net_index);
  connectivity_workspace.insert(connectivity::Pin{.pad = entity_id, .net = net_id});
}

} // namespace

ViaImportResult import_vias(const SExpr& kicad_pcb,
                            const stackup::StackupSnapshot& stackup,
                            const connectivity::ConnectivitySnapshot& nets,
                            const geometry::GeometrySnapshot& geometry_base) {
  const std::map<std::string, core::EntityId> net_index = build_net_index(nets);

  geometry::GeometryWorkspace geometry_workspace(geometry_base);
  connectivity::ConnectivityWorkspace connectivity_workspace(nets);

  for (const SExpr* via : find_all_children(kicad_pcb, "via")) {
    import_via_entry(*via, stackup, net_index, geometry_workspace, connectivity_workspace);
  }

  return ViaImportResult{.geometry = geometry_workspace.commit(),
                         .connectivity = connectivity_workspace.commit()};
}

} // namespace pcbir::kicad
