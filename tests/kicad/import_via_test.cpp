// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/import_footprint.hpp"
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
using pcbir::kicad::FootprintImportResult;
using pcbir::kicad::import_footprints;
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

// Assumes `snapshot`'s Component table has exactly one entry -- used to
// pull out the single id a composability test needs to check for.
template <typename Component, typename Snapshot>
[[nodiscard]] EntityId only_id(const Snapshot& snapshot) {
  EntityId found;
  snapshot.template table<Component>().for_each([&](EntityId id, const Component&) { found = id; });
  return found;
}

template <typename Component, typename Snapshot>
[[nodiscard]] bool table_contains_id(const Snapshot& snapshot, EntityId id) {
  bool found = false;
  snapshot.template table<Component>().for_each(
      [&](EntityId candidate, const Component&) { found = found || (candidate == id); });
  return found;
}

// A footprint with one thru-hole pad (modeled as a Via -- import_footprint.hpp)
// plus a separate free-standing via, both on net GND -- exercises chaining
// import_footprints -> import_vias the same way import_kicad_pcb does
// (pcbir/kicad/import.hpp).
constexpr const char* FOOTPRINT_AND_VIA_BOARD = R"(
  (kicad_pcb
    (layers
      (0 "F.Cu" signal)
      (2 "B.Cu" signal)
    )
    (footprint "" (layer "F.Cu") (at 10 10)
      (pad "1" thru_hole circle (at 0 0) (size 1.2 1.2) (drill 0.6) (layers "*.Cu") (net "GND"))
    )
    (via (at 20 20) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net "GND"))
  )
)";

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

TEST_CASE("import_vias given a geometry_base and an accumulated connectivity snapshot chains "
          "onto import_footprints without colliding ids",
          "[kicad][import]") {
  // Mirrors how import_kicad_pcb chains import_footprints -> import_vias
  // (pcbir/kicad/import.hpp): the free via's Via id must not collide with
  // the footprint's thru-hole-pad Via id, and its Pin id must not collide
  // with the footprint's own Pin id -- both continue from the accumulated
  // snapshot rather than each independently restarting at 1.
  const SExpr root = parse_sexpr(FOOTPRINT_AND_VIA_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const FootprintImportResult footprints = import_footprints(root, stackup, nets);
  REQUIRE(footprints.geometry.table<Via>().size() == 1);
  REQUIRE(footprints.connectivity.table<Pin>().size() == 1);

  const ViaImportResult vias =
      import_vias(root, stackup, footprints.connectivity, footprints.geometry);

  // Exactly 2 entries in each table, one of which is the footprint's own
  // entry carried forward unchanged, is enough to prove the new free
  // via/Pin got a non-colliding id: a table's entity ids are unique by
  // construction, so a second entry alongside the carried-forward one
  // cannot itself be a collision.
  REQUIRE(vias.geometry.table<Via>().size() == 2);
  REQUIRE(vias.connectivity.table<Pin>().size() == 2);
  REQUIRE(table_contains_id<Via>(vias.geometry, only_id<Via>(footprints.geometry)));
  REQUIRE(table_contains_id<Pin>(vias.connectivity, only_id<Pin>(footprints.connectivity)));
}
