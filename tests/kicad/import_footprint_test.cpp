// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/import_footprint.hpp"
#include "pcbir/kicad/import_nets.hpp"
#include "pcbir/kicad/sexpr.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::core::EntityId;
using pcbir::kicad::FootprintImportResult;
using pcbir::kicad::import_footprints;
using pcbir::kicad::import_nets;
using pcbir::kicad::import_stackup;
using pcbir::kicad::ImportError;
using pcbir::kicad::parse_sexpr;
using pcbir::kicad::SExpr;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::StackupSnapshot;

using pcbir::geometry::Footprint;
using pcbir::geometry::FootprintSide;
using pcbir::geometry::GeometrySnapshot;
using pcbir::geometry::Pad;
using pcbir::geometry::Via;

namespace {

// A 2-layer board with one front-side footprint (R1) mixing an SMD pad
// (net SIG1) and a thru_hole pad (net GND), plus a mechanical pad with no
// net at all -- verified against real pcbnew output
// (docs/rfcs/0003-kicad-importer-exporter.md).
constexpr const char* FRONT_FOOTPRINT_BOARD = R"(
  (kicad_pcb
    (layers
      (0 "F.Cu" signal)
      (2 "B.Cu" signal)
    )
    (footprint ""
      (layer "F.Cu")
      (at 10 10)
      (property "Reference" "R1" (at 0 0) (layer "F.SilkS"))
      (property "Value" "10k" (at 0 0) (layer "F.Fab"))
      (pad "1" smd rect (at -1 0) (size 1.6 1.2) (layers "F.Cu" "F.Mask") (net "SIG1"))
      (pad "2" thru_hole circle (at 1 0) (size 1 1) (drill 0.5) (layers "*.Cu") (net "GND"))
      (pad "MP" np_thru_hole circle (at 0 2) (size 2 2) (drill 1.5) (layers "*.Cu"))
    )
  )
)";

// The same footprint moved to the back side, with a 90-degree footprint
// rotation and a nonzero pad-local rotation -- exercises both the
// footprint-rotation-affects-pad-position and pad-rotation-is-absolute
// rules (docs/rfcs/0003-kicad-importer-exporter.md).
constexpr const char* BACK_ROTATED_FOOTPRINT_BOARD = R"(
  (kicad_pcb
    (layers
      (0 "F.Cu" signal)
      (2 "B.Cu" signal)
    )
    (footprint ""
      (layer "B.Cu")
      (at 10 10 90)
      (pad "1" smd rect (at 5 0 30) (size 1.6 1.2) (layers "B.Cu"))
    )
  )
)";

struct Imported {
  StackupSnapshot stackup;
  ConnectivitySnapshot nets;
  GeometrySnapshot geometry;
  ConnectivitySnapshot connectivity;
};

[[nodiscard]] Imported import_all(const char* board_text) {
  const SExpr root = parse_sexpr(board_text);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  auto result = import_footprints(root, stackup, nets);
  return Imported{.stackup = stackup,
                  .nets = nets,
                  .geometry = std::move(result.geometry),
                  .connectivity = std::move(result.connectivity)};
}

[[nodiscard]] const Footprint& only_footprint(const GeometrySnapshot& geometry) {
  const Footprint* found = nullptr;
  geometry.table<Footprint>().for_each(
      [&](EntityId, const Footprint& footprint) { found = &footprint; });
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] EntityId net_id_by_name(const ConnectivitySnapshot& nets, const std::string& name) {
  EntityId found;
  nets.table<Net>().for_each([&](EntityId id, const Net& net) {
    if (net.name == name) {
      found = id;
    }
  });
  return found;
}

[[nodiscard]] EntityId pin_net_for_pad(const ConnectivitySnapshot& connectivity, EntityId pad_id) {
  EntityId found;
  connectivity.table<Pin>().for_each([&](EntityId, const Pin& pin) {
    if (pin.pad == pad_id) {
      found = pin.net;
    }
  });
  return found;
}

// Extracted (rather than inlined per-TEST_CASE with a for_each lambda) so
// the lookup's own nesting doesn't count against the calling TEST_CASE's
// readability-function-cognitive-complexity budget.
[[nodiscard]] const Pad& only_pad(const GeometrySnapshot& geometry) {
  const Pad* found = nullptr;
  geometry.table<Pad>().for_each([&](EntityId, const Pad& pad) { found = &pad; });
  REQUIRE(found != nullptr);
  return *found;
}

// pad_number is std::string_view (not const std::string&) so that a call
// like via_by_pad_number(geometry, "2") doesn't bind a temporary std::string
// to the parameter -- GCC's -Wdangling-reference flags that pattern for any
// function returning a reference, even though the temporary actually
// outlives this call (verified false positive on GCC 13, Ubuntu CI).
[[nodiscard]] const Via& via_by_pad_number(const GeometrySnapshot& geometry,
                                           std::string_view pad_number) {
  const Via* found = nullptr;
  geometry.table<Via>().for_each([&](EntityId, const Via& via) {
    if (via.pad_number == pad_number) {
      found = &via;
    }
  });
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] EntityId front_copper_layer_id(const StackupSnapshot& stackup) {
  EntityId found;
  stackup.table<Layer>().for_each([&](EntityId id, const Layer& layer) {
    if (layer.kind == LayerKind::Copper && layer.name == "F.Cu") {
      found = id;
    }
  });
  return found;
}

} // namespace

TEST_CASE("import_footprints imports one Footprint with the right reference/value/position",
          "[kicad][import]") {
  const Imported imported = import_all(FRONT_FOOTPRINT_BOARD);

  REQUIRE(imported.geometry.table<Footprint>().size() == 1);
  const Footprint& footprint = only_footprint(imported.geometry);
  REQUIRE(footprint.reference_designator == "R1");
  REQUIRE(footprint.value == "10k");
  REQUIRE(footprint.position.x == 10'000'000);
  REQUIRE(footprint.position.y == 10'000'000);
  REQUIRE(footprint.side == FootprintSide::Top);
  REQUIRE(footprint.pads.size() == 3);
}

TEST_CASE("import_footprints imports an smd pad as a Pad with resolved absolute position",
          "[kicad][import]") {
  const Imported imported = import_all(FRONT_FOOTPRINT_BOARD);

  REQUIRE(imported.geometry.table<Pad>().size() == 1);
  const Pad& pad = only_pad(imported.geometry);
  REQUIRE(pad.pad_number == "1");
  // Footprint at (10, 10)mm, no rotation; pad local offset (-1, 0)mm.
  REQUIRE(pad.position.x == 9'000'000);
  REQUIRE(pad.position.y == 10'000'000);
  REQUIRE(pcbir::geometry::validate(pad.outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(pad.layer == front_copper_layer_id(imported.stackup));
}

TEST_CASE("import_footprints imports a thru_hole pad as a Via spanning every copper layer",
          "[kicad][import]") {
  const Imported imported = import_all(FRONT_FOOTPRINT_BOARD);

  // 1 real thru_hole pad ("2") + 1 mechanical np_thru_hole pad ("MP").
  REQUIRE(imported.geometry.table<Via>().size() == 2);

  const Via& via2 = via_by_pad_number(imported.geometry, "2");
  REQUIRE(via2.position.x == 11'000'000);
  REQUIRE(via2.position.y == 10'000'000);
  REQUIRE(via2.drill_diameter_nm == 500'000);
  REQUIRE(via2.pad_diameter_nm == 1'000'000);
  REQUIRE_FALSE(via2.start_layer.is_null());
  REQUIRE_FALSE(via2.end_layer.is_null());
  REQUIRE_FALSE(via2.start_layer == via2.end_layer);
}

TEST_CASE("import_footprints resolves a thru_hole pad whose (layers ...) also lists "
          "non-copper wildcard entries",
          "[kicad][import]") {
  // Real pcbnew output for a plated through-hole pad with mask clearance
  // on both sides writes (layers "*.Cu" "*.Mask") -- resolve_via_layer_
  // span (coordinate_util.hpp) must pick out just the copper entry, not
  // treat "*.Mask" as a second copper layer name to resolve.
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal) (2 "B.Cu" signal))
           (footprint "" (layer "F.Cu") (at 0 0)
             (pad "1" thru_hole circle (at 0 0) (size 1 1) (drill 0.6)
               (layers "*.Cu" "*.Mask")))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const FootprintImportResult result = import_footprints(root, stackup, nets);

  REQUIRE(result.geometry.table<Via>().size() == 1);
  const Via* via = nullptr;
  result.geometry.table<Via>().for_each([&](EntityId, const Via& candidate) { via = &candidate; });
  REQUIRE(via != nullptr);
  REQUIRE_FALSE(via->start_layer.is_null());
  REQUIRE_FALSE(via->end_layer.is_null());
  REQUIRE_FALSE(via->start_layer == via->end_layer);
}

TEST_CASE("import_footprints links each pad/via to its net via a Pin, null for no net",
          "[kicad][import]") {
  const Imported imported = import_all(FRONT_FOOTPRINT_BOARD);

  REQUIRE(imported.connectivity.table<Pin>().size() == 3);

  const Footprint& footprint = only_footprint(imported.geometry);
  const EntityId sig1 = net_id_by_name(imported.nets, "SIG1");
  const EntityId gnd = net_id_by_name(imported.nets, "GND");
  REQUIRE_FALSE(sig1.is_null());
  REQUIRE_FALSE(gnd.is_null());

  // pads[0] = "1" (SIG1), pads[1] = "2" (GND), pads[2] = "MP" (no net).
  REQUIRE(pin_net_for_pad(imported.connectivity, footprint.pads.at(0)) == sig1);
  REQUIRE(pin_net_for_pad(imported.connectivity, footprint.pads.at(1)) == gnd);
  REQUIRE(pin_net_for_pad(imported.connectivity, footprint.pads.at(2)).is_null());
}

TEST_CASE("import_footprints sets FootprintSide::Bottom for a B.Cu footprint", "[kicad][import]") {
  const Imported imported = import_all(BACK_ROTATED_FOOTPRINT_BOARD);

  REQUIRE(only_footprint(imported.geometry).side == FootprintSide::Bottom);
}

TEST_CASE("import_footprints applies the footprint's own rotation to a pad's position",
          "[kicad][import]") {
  const Imported imported = import_all(BACK_ROTATED_FOOTPRINT_BOARD);

  const Pad* pad = nullptr;
  imported.geometry.table<Pad>().for_each(
      [&](EntityId, const Pad& candidate) { pad = &candidate; });
  REQUIRE(pad != nullptr);
  // Footprint at (10, 10)mm rotated 90 degrees (KiCad's own on-disk
  // convention); pad local offset (5, 0)mm. Verified against real pcbnew
  // output (docs/rfcs/0003-kicad-importer-exporter.md): rotating (+X, 0)
  // by a stored +90 moves it to absolute (0, -X) relative to the
  // footprint's own position.
  REQUIRE(pad->position.x == 10'000'000);
  REQUIRE(pad->position.y == 5'000'000);
}

TEST_CASE("import_footprints rejects a custom pad shape missing (primitives ...)",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal)) (footprint "" (layer "F.Cu") (at 0 0)
           (pad "1" smd custom (at 0 0) (size 1 1) (layers "F.Cu")))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  REQUIRE_THROWS_AS(import_footprints(root, stackup, nets), ImportError);
}

TEST_CASE("import_footprints rejects a custom pad with more than one primitive",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal)) (footprint "" (layer "F.Cu") (at 0 0)
           (pad "1" smd custom (at 0 0) (size 1 1) (layers "F.Cu")
             (primitives
               (gr_poly (pts (xy 0 0) (xy 1 0) (xy 1 1)) (width 0))
               (gr_poly (pts (xy -1 -1) (xy -1 0) (xy 0 -1)) (width 0)))))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  REQUIRE_THROWS_AS(import_footprints(root, stackup, nets), ImportError);
}

TEST_CASE("import_footprints imports a custom pad's single gr_poly primitive as its outline",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal)) (footprint "" (layer "F.Cu") (at 10 5)
           (pad "1" smd custom (at 2 1) (size 0.5 0.5) (layers "F.Cu")
             (primitives
               (gr_poly (pts (xy -0.8 -0.6) (xy 0.8 -0.6) (xy 0.8 0.6) (xy -0.8 0.6)) (width 0)))))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  const FootprintImportResult result = import_footprints(root, stackup, nets);

  REQUIRE(result.geometry.table<Pad>().size() == 1);
  const Pad* pad = nullptr;
  result.geometry.table<Pad>().for_each([&](EntityId, const Pad& candidate) { pad = &candidate; });
  REQUIRE(pad != nullptr);
  REQUIRE(pad->position.x == 12'000'000);
  REQUIRE(pad->position.y == 6'000'000);
  REQUIRE(pcbir::geometry::validate(pad->outline) == pcbir::geometry::PolygonValidity::Valid);
  REQUIRE(pad->outline.outline.spans.size() == 4);
}

TEST_CASE("import_footprints rejects an unrecognized pad type", "[kicad][import]") {
  const SExpr root = parse_sexpr(
      R"((kicad_pcb (layers (0 "F.Cu" signal)) (footprint "" (layer "F.Cu") (at 0 0)
           (pad "1" edge_connector rect (at 0 0) (size 1 1) (layers "F.Cu")))))");
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);
  REQUIRE_THROWS_AS(import_footprints(root, stackup, nets), ImportError);
}

TEST_CASE("import_footprints throws when a footprint's own net table is missing an entry",
          "[kicad][import]") {
  const SExpr root = parse_sexpr(FRONT_FOOTPRINT_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot empty_nets = import_nets(parse_sexpr("(kicad_pcb)"));
  REQUIRE_THROWS_AS(import_footprints(root, stackup, empty_nets), ImportError);
}

TEST_CASE("import_footprints given a geometry_base continues its id space instead of "
          "restarting at 1",
          "[kicad][import]") {
  // Mirrors how import_kicad_pcb chains composable passes together
  // (pcbir/kicad/import.hpp): a prior pass's geometry (here, standing in
  // for import_board_outline's output) must not have any of its ids
  // reused by this pass's own new Pad/Via/Footprint entities.
  const SExpr root = parse_sexpr(FRONT_FOOTPRINT_BOARD);
  const StackupSnapshot stackup = import_stackup(root);
  const ConnectivitySnapshot nets = import_nets(root);

  const FootprintImportResult first = import_footprints(root, stackup, nets);
  const FootprintImportResult second = import_footprints(root, stackup, nets, first.geometry);

  REQUIRE(first.geometry.table<Footprint>().size() == 1);
  REQUIRE(second.geometry.table<Footprint>().size() == 2);

  const EntityId first_pad_id = only_footprint(first.geometry).pads.at(0);
  bool first_pad_id_carried_forward = false;
  bool no_new_entity_reuses_it = true;
  second.geometry.table<Pad>().for_each([&](EntityId id, const Pad&) {
    if (id == first_pad_id) {
      first_pad_id_carried_forward = true;
    }
  });
  second.geometry.table<Via>().for_each([&](EntityId id, const Via&) {
    no_new_entity_reuses_it = no_new_entity_reuses_it && (id != first_pad_id);
  });
  int footprints_matching_first_pad = 0;
  second.geometry.table<Footprint>().for_each([&](EntityId id, const Footprint&) {
    footprints_matching_first_pad += (id == first_pad_id) ? 1 : 0;
  });
  REQUIRE(first_pad_id_carried_forward);
  REQUIRE(no_new_entity_reuses_it);
  REQUIRE(footprints_matching_first_pad == 0);
}
