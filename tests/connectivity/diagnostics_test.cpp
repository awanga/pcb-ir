// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/bus.hpp"
#include "pcbir/connectivity/diagnostics.hpp"
#include "pcbir/connectivity/differential_pair.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::Bus;
using pcbir::connectivity::ConnectivityWorkspace;
using pcbir::connectivity::Diagnostic;
using pcbir::connectivity::DiagnosticCode;
using pcbir::connectivity::DifferentialPair;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::connectivity::validate;
using pcbir::core::EntityId;

namespace {

bool has(const std::vector<Diagnostic>& diagnostics, EntityId id, DiagnosticCode code) {
  return std::ranges::any_of(diagnostics, [&](const Diagnostic& diagnostic) {
    return diagnostic.id == id && diagnostic.code == code;
  });
}

} // namespace

TEST_CASE("Net validation rejects an empty name", "[connectivity][diagnostics]") {
  REQUIRE(validate(Net{.name = "GND"}) == DiagnosticCode::Valid);
  REQUIRE(validate(Net{.name = ""}) == DiagnosticCode::EmptyNetName);
}

TEST_CASE("Pin validation distinguishes a missing pad from an unconnected net",
          "[connectivity][diagnostics]") {
  REQUIRE(validate(Pin{.pad = EntityId{1}, .net = EntityId{2}}) == DiagnosticCode::Valid);
  REQUIRE(validate(Pin{.pad = EntityId{}, .net = EntityId{2}}) == DiagnosticCode::InvalidPin);
  REQUIRE(validate(Pin{.pad = EntityId{1}, .net = EntityId{}}) == DiagnosticCode::UnconnectedPin);
}

TEST_CASE("DifferentialPair validation rejects null or identical members",
          "[connectivity][diagnostics]") {
  REQUIRE(validate(DifferentialPair{.positive_net = EntityId{1}, .negative_net = EntityId{2}}) ==
          DiagnosticCode::Valid);
  REQUIRE(validate(DifferentialPair{.positive_net = EntityId{}, .negative_net = EntityId{2}}) ==
          DiagnosticCode::DegenerateDiffPair);
  REQUIRE(validate(DifferentialPair{.positive_net = EntityId{1}, .negative_net = EntityId{1}}) ==
          DiagnosticCode::DegenerateDiffPair);
}

TEST_CASE("Bus validation rejects empty and duplicate membership", "[connectivity][diagnostics]") {
  REQUIRE(validate(Bus{.name = "D", .members = {EntityId{1}, EntityId{2}}}) ==
          DiagnosticCode::Valid);
  REQUIRE(validate(Bus{.name = "D", .members = {}}) == DiagnosticCode::EmptyBus);
  REQUIRE(validate(Bus{.name = "D", .members = {EntityId{1}, EntityId{1}}}) ==
          DiagnosticCode::DuplicateBusMember);
}

TEST_CASE("A fully-connected snapshot reports no diagnostics", "[connectivity][diagnostics]") {
  ConnectivityWorkspace workspace;
  const auto gnd = workspace.insert(Net{.name = "GND"});
  const EntityId gnd_id = workspace.table<Net>().id_of(gnd);
  workspace.insert(Pin{.pad = EntityId{100}, .net = gnd_id});

  const std::vector<Diagnostic> diagnostics = validate(workspace.commit());
  REQUIRE(diagnostics.empty());
}

TEST_CASE("Validating a snapshot flags a Pin referencing a net that doesn't exist",
          "[connectivity][diagnostics]") {
  ConnectivityWorkspace workspace;
  const auto pin = workspace.insert(Pin{.pad = EntityId{100}, .net = EntityId{999}});
  const EntityId pin_id = workspace.table<Pin>().id_of(pin);

  const std::vector<Diagnostic> diagnostics = validate(workspace.commit());
  REQUIRE(has(diagnostics, pin_id, DiagnosticCode::DanglingPinNetReference));
}

TEST_CASE("Validating a snapshot flags two Pins sharing the same pad as a short",
          "[connectivity][diagnostics]") {
  ConnectivityWorkspace workspace;
  const auto net_a = workspace.insert(Net{.name = "A"});
  const auto net_b = workspace.insert(Net{.name = "B"});
  const EntityId net_a_id = workspace.table<Net>().id_of(net_a);
  const EntityId net_b_id = workspace.table<Net>().id_of(net_b);

  workspace.insert(Pin{.pad = EntityId{100}, .net = net_a_id});
  const auto second_pin = workspace.insert(Pin{.pad = EntityId{100}, .net = net_b_id});
  const EntityId second_pin_id = workspace.table<Pin>().id_of(second_pin);

  const std::vector<Diagnostic> diagnostics = validate(workspace.commit());
  REQUIRE(has(diagnostics, second_pin_id, DiagnosticCode::DuplicatePadAssignment));
}

TEST_CASE("Validating a snapshot flags a diff pair member net that doesn't exist",
          "[connectivity][diagnostics]") {
  ConnectivityWorkspace workspace;
  const auto net_a = workspace.insert(Net{.name = "A"});
  const EntityId net_a_id = workspace.table<Net>().id_of(net_a);
  const auto pair =
      workspace.insert(DifferentialPair{.positive_net = net_a_id, .negative_net = EntityId{999}});
  const EntityId pair_id = workspace.table<DifferentialPair>().id_of(pair);

  const std::vector<Diagnostic> diagnostics = validate(workspace.commit());
  REQUIRE(has(diagnostics, pair_id, DiagnosticCode::DanglingDiffPairMember));
}

TEST_CASE("Validating a snapshot flags a bus member net that doesn't exist",
          "[connectivity][diagnostics]") {
  ConnectivityWorkspace workspace;
  const auto net_a = workspace.insert(Net{.name = "A"});
  const EntityId net_a_id = workspace.table<Net>().id_of(net_a);
  const auto bus = workspace.insert(Bus{.name = "D", .members = {net_a_id, EntityId{999}}});
  const EntityId bus_id = workspace.table<Bus>().id_of(bus);

  const std::vector<Diagnostic> diagnostics = validate(workspace.commit());
  REQUIRE(has(diagnostics, bus_id, DiagnosticCode::DanglingBusMember));
}
