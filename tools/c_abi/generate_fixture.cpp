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
#include <exception>
#include <fstream>
#include <ios>
#include <iostream>
#include <span>
#include <vector>

namespace {

int run(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<size_t>(argc));
  if (args.size() != 2) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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
                           .end_layer = pcbir::core::EntityId{2},
                           .pad_number = "1"});

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
  // args.size() == 2 is checked above, so index 1 is in bounds.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  std::ofstream file(args[1], std::ios::binary | std::ios::trunc);
  if (!file) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    std::cerr << "failed to open " << args[1] << " for writing\n";
    return 1;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return file ? 0 : 1;
}

} // namespace

// bugprone-exception-escape's static analysis traces a std::bad_cast frame through
// std::ofstream::open()'s locale-facet lookup (MSVC's <fstream>/<xlocale>) that it cannot prove
// is caught here, even though bad_cast derives from std::exception and both catch clauses below
// are exhaustive. Verified by inspection, not by suppressing a real path: nothing in run() can
// escape past a catch-all.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "generate_fixture: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "generate_fixture: unknown error\n";
    return 1;
  }
}
