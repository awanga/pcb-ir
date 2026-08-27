// SPDX-License-Identifier: Apache-2.0
//
// Builds a small, valid sample board in memory and serializes it (via
// pcbir::serialize) to the path given on argv[1]. Used only to produce the
// C ABI smoke test's fixture at build/test time -- never committed as a
// binary (AGENTS.md: "Do not commit: generated binaries").
#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/serialize.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/material.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <span>
#include <vector>

int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<size_t>(argc));
  if (args.size() != 2) {
    std::cerr << "usage: " << (args.empty() ? "generate_fixture" : args[0]) << " <output-path>\n";
    return 1;
  }

  pcbir::geometry::GeometryWorkspace geometry_workspace;
  geometry_workspace.insert(
      pcbir::geometry::Via{.position = pcbir::geometry::Point{.x = 1000000, .y = 2000000},
                           .drill_diameter_nm = 200000,
                           .finished_hole_diameter_nm = 250000,
                           .pad_diameter_nm = 450000,
                           .start_layer = pcbir::core::EntityId{1},
                           .end_layer = pcbir::core::EntityId{2}});

  pcbir::connectivity::ConnectivityWorkspace connectivity_workspace;
  const auto net = connectivity_workspace.insert(pcbir::connectivity::Net{.name = "GND"});
  const pcbir::core::EntityId net_id =
      connectivity_workspace.table<pcbir::connectivity::Net>().id_of(net);
  connectivity_workspace.insert(
      pcbir::connectivity::Pin{.pad = pcbir::core::EntityId{100}, .net = net_id});

  pcbir::stackup::StackupWorkspace stackup_workspace;
  const auto material = stackup_workspace.insert(pcbir::stackup::Material{
      .name = "FR4", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = 20000});
  const pcbir::core::EntityId material_id =
      stackup_workspace.table<pcbir::stackup::Material>().id_of(material);
  stackup_workspace.insert(pcbir::stackup::Layer{.name = "Core1",
                                                 .kind = pcbir::stackup::LayerKind::Dielectric,
                                                 .thickness_nm = 200000,
                                                 .roughness_nm = 0,
                                                 .material = material_id});

  pcbir::BoardSnapshot board;
  board.geometry = geometry_workspace.commit();
  board.connectivity = connectivity_workspace.commit();
  board.stackup = stackup_workspace.commit();

  const std::vector<uint8_t> bytes = pcbir::serialize(board);
  std::ofstream file(args[1], std::ios::binary | std::ios::trunc);
  if (!file) {
    std::cerr << "failed to open " << args[1] << " for writing\n";
    return 1;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return file ? 0 : 1;
}
