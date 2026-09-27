// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/kicad/import_nets.hpp"
#include "pcbir/kicad/sexpr.hpp"

#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::Net;
using pcbir::core::EntityId;
using pcbir::kicad::import_nets;
using pcbir::kicad::parse_sexpr;

namespace {

[[nodiscard]] std::set<std::string> net_names(const ConnectivitySnapshot& snapshot) {
  std::set<std::string> names;
  snapshot.table<Net>().for_each([&](EntityId, const Net& net) { names.insert(net.name); });
  return names;
}

// A board with nets referenced from a pad (nested inside a footprint), a
// segment, an arc, a via, and a zone -- exercising every place a
// `(net "NAME")` entry can appear, deliberately out of alphabetical order
// and with SIG1 repeated (pad + segment) to test deduplication.
constexpr const char* BOARD = R"(
  (kicad_pcb
    (footprint ""
      (layer "F.Cu")
      (pad "1" smd rect (at -1 0) (size 1 1) (layers "F.Cu") (net "SIG1"))
      (pad "2" thru_hole circle (at 1 0) (size 1 1) (drill 0.5) (layers "*.Cu") (net "GND"))
    )
    (segment (start 0 0) (end 1 1) (width 0.25) (layer "F.Cu") (net "SIG1"))
    (arc (start 0 0) (mid 1 1) (end 2 0) (width 0.25) (layer "F.Cu") (net "CLK"))
    (via (at 5 5) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net "GND"))
    (zone (net "PWR") (layer "F.Cu") (polygon (pts (xy 0 0) (xy 1 0) (xy 1 1))))
  )
)";

} // namespace

TEST_CASE("import_nets collects every distinct net name referenced anywhere in the file",
          "[kicad][import]") {
  const ConnectivitySnapshot snapshot = import_nets(parse_sexpr(BOARD));

  REQUIRE(net_names(snapshot) == std::set<std::string>{"CLK", "GND", "PWR", "SIG1"});
}

TEST_CASE("import_nets deduplicates a net referenced from more than one entity",
          "[kicad][import]") {
  const ConnectivitySnapshot snapshot = import_nets(parse_sexpr(BOARD));

  // SIG1 is referenced by both a pad and a segment above; GND by both a
  // pad and a via -- neither should produce a duplicate Net.
  REQUIRE(snapshot.table<Net>().size() == 4);
}

TEST_CASE("import_nets ignores an unconnected pad with no (net ...) entry at all",
          "[kicad][import]") {
  const ConnectivitySnapshot snapshot =
      import_nets(parse_sexpr(R"((kicad_pcb (footprint "" (pad "1" smd circle
        (at 0 0) (size 1 1) (layers "F.Cu")))))"));

  REQUIRE(snapshot.table<Net>().size() == 0);
}

TEST_CASE("import_nets produces no Net for an empty file", "[kicad][import]") {
  const ConnectivitySnapshot snapshot = import_nets(parse_sexpr("(kicad_pcb)"));

  REQUIRE(snapshot.table<Net>().size() == 0);
}
