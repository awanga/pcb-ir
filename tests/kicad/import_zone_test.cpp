// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/import_nets.hpp"
#include "pcbir/kicad/import_zone.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::Net;
using pcbir::core::EntityId;
using pcbir::kicad::import_nets;
using pcbir::kicad::import_stackup;
using pcbir::kicad::import_zones;
using pcbir::kicad::ImportError;
using pcbir::kicad::parse_sexpr;
using pcbir::kicad::SExpr;
using pcbir::stackup::Layer;
using pcbir::stackup::StackupSnapshot;

using pcbir::geometry::CopperPour;
using pcbir::geometry::GeometrySnapshot;

namespace {

// A single-layer 4-point rectangular zone on F.Cu and a two-layer zone
// (poured on both F.Cu and B.Cu) with no net -- verified against real
// pcbnew output (docs/rfcs/0003-kicad-importer-exporter.md).
constexpr const char* ZONE_BOARD = R"(
  (kicad_pcb
    (layers (0 "F.Cu" signal) (2 "B.Cu" signal))
    (zone
      (net "GND")
      (layer "F.Cu")
      (polygon (pts (xy 0 0) (xy 30 0) (xy 30 20) (xy 0 20)))
    )
    (zone
      (layers "F.Cu" "B.Cu")
      (polygon (pts (xy 0 0) (xy 10 0) (xy 10 10) (xy 0 10)))
    )
  )
)";

[[nodiscard]] EntityId layer_id_by_name(const StackupSnapshot& stackup, const std::string& name) {
  EntityId found;
  stackup.table<Layer>().for_each([&](EntityId id, const Layer& layer) {
    if (layer.name == name) {
      found = id;
    }
  });
  return found;
}

[[nodiscard]] const CopperPour& pour_on_layer(const GeometrySnapshot& geometry, EntityId layer_id) {
  const CopperPour* found = nullptr;
  geometry.table<CopperPour>().for_each([&](EntityId, const CopperPour& pour) {
    if (pour.layer == layer_id) {
      found = &pour;
    }
  });
  REQUIRE(found != nullptr);
  return *found;
}

} // namespace

TEST_CASE("import_zones imports a single-layer zone as one CopperPour", "[kicad][import]") {
  const SExpr root = parse_sexpr(ZONE_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_zones(root, stackup, nets);

  // 1 single-layer zone + 1 two-layer zone (split into 2 CopperPours) = 3.
  REQUIRE(geometry.table<CopperPour>().size() == 3);
}

TEST_CASE("import_zones resolves a zone's outline, layer, and net", "[kicad][import]") {
  const SExpr root = parse_sexpr(ZONE_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_zones(root, stackup, nets);

  const CopperPour& pour = pour_on_layer(geometry, layer_id_by_name(stackup, "F.Cu"));
  REQUIRE(pcbir::geometry::validate(pour.outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(pour.outline.outline.spans.size() == 4);
  REQUIRE(pour.outline.holes.empty());

  EntityId gnd;
  nets.table<Net>().for_each([&](EntityId id, const Net& net) {
    if (net.name == "GND") {
      gnd = id;
    }
  });
  REQUIRE_FALSE(gnd.is_null());
}

TEST_CASE("import_zones splits a multi-layer zone into one CopperPour per layer, "
          "sharing the same outline",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(ZONE_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_zones(root, stackup, nets);

  const CopperPour& front = pour_on_layer(geometry, layer_id_by_name(stackup, "F.Cu"));
  const CopperPour& back = pour_on_layer(geometry, layer_id_by_name(stackup, "B.Cu"));
  REQUIRE(front.outline.outline.spans.size() == 4);
  REQUIRE(back.outline.outline.spans.size() == 4);
}

TEST_CASE("import_zones leaves an unassigned zone's net null", "[kicad][import]") {
  const SExpr root = parse_sexpr(ZONE_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_zones(root, stackup, nets);

  const CopperPour& back = pour_on_layer(geometry, layer_id_by_name(stackup, "B.Cu"));
  REQUIRE(back.net.is_null());
}

TEST_CASE("import_zones assembles a zone with a defined cutout as a hole", "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal))
           (zone (layer "F.Cu")
             (polygon (pts (xy 0 0) (xy 30 0) (xy 30 20) (xy 0 20)))
             (polygon (pts (xy 5 5) (xy 10 5) (xy 10 10) (xy 5 10))))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot geometry = import_zones(root, stackup, nets);

  REQUIRE(geometry.table<CopperPour>().size() == 1);
  const CopperPour& pour = pour_on_layer(geometry, layer_id_by_name(stackup, "F.Cu"));
  REQUIRE(pcbir::geometry::validate(pour.outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(pour.outline.holes.size() == 1);
}

TEST_CASE("import_zones throws on a zone missing (polygon ...)", "[kicad][import]") {
  const SExpr root = parse_sexpr(R"((kicad_pcb (layers (0 "F.Cu" signal)) (zone (layer "F.Cu"))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  REQUIRE_THROWS_AS(import_zones(root, stackup, nets), ImportError);
}

TEST_CASE("import_zones given a base snapshot continues its id space instead of restarting at 1",
          "[kicad][import]") {
  // Mirrors how import_kicad_pcb chains composable passes together
  // (pcbir/kicad/import.hpp).
  const SExpr root = parse_sexpr(ZONE_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const GeometrySnapshot first = import_zones(root, stackup, nets);
  const GeometrySnapshot second = import_zones(root, stackup, nets, first);

  REQUIRE(first.table<CopperPour>().size() == 3);
  REQUIRE(second.table<CopperPour>().size() == 6);

  bool no_collision = true;
  int seen_from_first = 0;
  first.table<CopperPour>().for_each([&](EntityId first_id, const CopperPour&) {
    int matches = 0;
    second.table<CopperPour>().for_each(
        [&](EntityId second_id, const CopperPour&) { matches += (second_id == first_id) ? 1 : 0; });
    seen_from_first += matches;
    no_collision = no_collision && matches <= 1;
  });
  REQUIRE(seen_from_first == 3); // every id from `first` carried forward unchanged
  REQUIRE(no_collision);         // and none of the 3 new CopperPours reused one of those ids
}

TEST_CASE("import_zones throws on an arc-cornered zone outline", "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal))
           (zone (layer "F.Cu")
             (polygon (pts (xy 0 0) (arc (start 1 0) (mid 1 1) (end 0 1)) (xy 0 1))))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  REQUIRE_THROWS_AS(import_zones(root, stackup, nets), ImportError);
}
