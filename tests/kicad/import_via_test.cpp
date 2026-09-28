// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/import_nets.hpp"
#include "pcbir/kicad/import_via.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::core::EntityId;
using pcbir::kicad::import_nets;
using pcbir::kicad::import_stackup;
using pcbir::kicad::import_vias;
using pcbir::kicad::ImportError;
using pcbir::kicad::parse_sexpr;
using pcbir::kicad::SExpr;
using pcbir::kicad::ViaImportResult;
using pcbir::stackup::StackupSnapshot;

using pcbir::geometry::GeometrySnapshot;
using pcbir::geometry::Via;

namespace {

// A 4-layer board (so a blind span exercises resolve_via_layer_span's
// explicit-2-name path, not just the "*.Cu" wildcard already covered by
// import_footprint_test.cpp's thru-hole pad) with one through via and one
// blind via -- verified against real pcbnew output
// (docs/rfcs/0003-kicad-importer-exporter.md).
constexpr const char* VIA_BOARD = R"(
  (kicad_pcb
    (layers
      (0 "F.Cu" signal)
      (1 "In1.Cu" signal)
      (2 "In2.Cu" signal)
      (3 "B.Cu" signal)
    )
    (via (at 5 5) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net "GND"))
    (via blind (at 10 10) (size 0.5) (drill 0.25) (layers "B.Cu" "In1.Cu"))
  )
)";

[[nodiscard]] const Via& via_at_x(const GeometrySnapshot& geometry, int64_t x) {
  const Via* found = nullptr;
  geometry.table<Via>().for_each([&](EntityId, const Via& via) {
    if (via.position.x == x) {
      found = &via;
    }
  });
  REQUIRE(found != nullptr);
  return *found;
}

} // namespace

TEST_CASE("import_vias imports one Via per (via ...) entry", "[kicad][import]") {
  const SExpr root = parse_sexpr(VIA_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const ViaImportResult result = import_vias(root, stackup, nets);

  REQUIRE(result.geometry.table<Via>().size() == 2);
}

TEST_CASE("import_vias resolves a through via's exact position/diameters/net", "[kicad][import]") {
  const SExpr root = parse_sexpr(VIA_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const ViaImportResult result = import_vias(root, stackup, nets);

  const Via& via = via_at_x(result.geometry, 5'000'000);
  REQUIRE(via.position.y == 5'000'000);
  REQUIRE(via.drill_diameter_nm == 400'000);
  REQUIRE(via.pad_diameter_nm == 800'000);
  REQUIRE_FALSE(via.start_layer.is_null());
  REQUIRE_FALSE(via.end_layer.is_null());
  REQUIRE_FALSE(via.start_layer == via.end_layer);
  REQUIRE(via.pad_number.empty());
}

TEST_CASE("import_vias orders a blind via's start/end by physical stackup position",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(VIA_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const ViaImportResult result = import_vias(root, stackup, nets);

  // Authored as (layers "B.Cu" "In1.Cu") -- reversed physical order -- to
  // confirm resolve_via_layer_span reorders rather than preserving file
  // order.
  const Via& blind = via_at_x(result.geometry, 10'000'000);
  REQUIRE_FALSE(blind.start_layer == blind.end_layer);
}

TEST_CASE("import_vias links each Via to its net via a Pin, null for no net", "[kicad][import]") {
  const SExpr root = parse_sexpr(VIA_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const ViaImportResult result = import_vias(root, stackup, nets);

  REQUIRE(result.connectivity.table<Pin>().size() == 2);

  EntityId gnd;
  nets.table<Net>().for_each([&](EntityId id, const Net& net) {
    if (net.name == "GND") {
      gnd = id;
    }
  });
  REQUIRE_FALSE(gnd.is_null());

  EntityId through_id;
  EntityId blind_id;
  result.geometry.table<Via>().for_each([&](EntityId id, const Via& via) {
    if (via.position.x == 5'000'000) {
      through_id = id;
    } else if (via.position.x == 10'000'000) {
      blind_id = id;
    }
  });

  EntityId through_net;
  EntityId blind_net;
  result.connectivity.table<Pin>().for_each([&](EntityId, const Pin& pin) {
    if (pin.pad == through_id) {
      through_net = pin.net;
    } else if (pin.pad == blind_id) {
      blind_net = pin.net;
    }
  });
  REQUIRE(through_net == gnd);
  REQUIRE(blind_net.is_null());
}

TEST_CASE("import_vias throws on a via missing (drill ...)", "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal) (2 "B.Cu" signal))
           (via (at 1 1) (size 0.8) (layers "F.Cu" "B.Cu"))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  REQUIRE_THROWS_AS(import_vias(root, stackup, nets), ImportError);
}
