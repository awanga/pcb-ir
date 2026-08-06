// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/bus.hpp"
#include "pcbir/connectivity/differential_pair.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::Bus;
using pcbir::connectivity::ConnectivitySnapshot;
using pcbir::connectivity::ConnectivityWorkspace;
using pcbir::connectivity::deserialize_connectivity;
using pcbir::connectivity::DifferentialPair;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::connectivity::serialize;
using pcbir::core::EntityId;

namespace {

// Handles to the entities build_sample_snapshot() creates, by name, so
// individual TEST_CASEs below can look up "the GND net" etc. after a round
// trip without depending on iteration order.
struct SampleIds {
  EntityId gnd_net;
  EntityId usb_p_net;
  EntityId usb_n_net;
  EntityId pin;
  EntityId diff_pair;
  EntityId bus;
};

ConnectivitySnapshot build_sample_snapshot(SampleIds& ids) {
  ConnectivityWorkspace workspace;

  const auto gnd = workspace.insert(Net{.name = "GND"});
  const auto usb_p = workspace.insert(Net{.name = "USB_D+"});
  const auto usb_n = workspace.insert(Net{.name = "USB_D-"});
  const auto pin = workspace.insert(Pin{.pad = EntityId{100}, .net = EntityId{}});
  const auto pair =
      workspace.insert(DifferentialPair{.positive_net = EntityId{}, .negative_net = EntityId{}});
  const auto bus = workspace.insert(Bus{.name = "D", .members = {}});

  ids.gnd_net = workspace.table<Net>().id_of(gnd);
  ids.usb_p_net = workspace.table<Net>().id_of(usb_p);
  ids.usb_n_net = workspace.table<Net>().id_of(usb_n);
  ids.pin = workspace.table<Pin>().id_of(pin);
  ids.diff_pair = workspace.table<DifferentialPair>().id_of(pair);
  ids.bus = workspace.table<Bus>().id_of(bus);

  // Now that the referenced nets have real ids, wire up the pin, diff
  // pair, and bus to reference them -- exercising every field with
  // non-null cross-references before round-tripping.
  Pin* pin_value = workspace.try_get(pin);
  pin_value->net = ids.gnd_net;

  DifferentialPair* pair_value = workspace.try_get(pair);
  pair_value->positive_net = ids.usb_p_net;
  pair_value->negative_net = ids.usb_n_net;

  Bus* bus_value = workspace.try_get(bus);
  bus_value->members = {ids.usb_p_net, ids.usb_n_net, ids.gnd_net};

  return workspace.commit();
}

ConnectivitySnapshot round_trip(const ConnectivitySnapshot& original) {
  const std::vector<uint8_t> bytes = serialize(original);
  ConnectivityWorkspace restored_workspace = deserialize_connectivity(bytes);
  return restored_workspace.commit();
}

} // namespace

TEST_CASE("A connectivity snapshot round-trip preserves every entity's count",
          "[connectivity][serialize]") {
  SampleIds ids;
  const ConnectivitySnapshot original = build_sample_snapshot(ids);
  const ConnectivitySnapshot restored = round_trip(original);

  REQUIRE(restored.table<Net>().size() == original.table<Net>().size());
  REQUIRE(restored.table<Pin>().size() == original.table<Pin>().size());
  REQUIRE(restored.table<DifferentialPair>().size() == original.table<DifferentialPair>().size());
  REQUIRE(restored.table<Bus>().size() == original.table<Bus>().size());
}

TEST_CASE("A Net round-trips its name", "[connectivity][serialize]") {
  SampleIds ids;
  const ConnectivitySnapshot original = build_sample_snapshot(ids);
  const ConnectivitySnapshot restored = round_trip(original);

  const Net* restored_net = restored.table<Net>().try_get(restored.table<Net>().find(ids.gnd_net));
  REQUIRE(restored_net != nullptr);
  REQUIRE(restored_net->name == "GND");
}

TEST_CASE("A Pin round-trips its pad and net references", "[connectivity][serialize]") {
  SampleIds ids;
  const ConnectivitySnapshot original = build_sample_snapshot(ids);
  const ConnectivitySnapshot restored = round_trip(original);

  const Pin* restored_pin = restored.table<Pin>().try_get(restored.table<Pin>().find(ids.pin));
  REQUIRE(restored_pin != nullptr);
  REQUIRE(restored_pin->pad == EntityId{100});
  REQUIRE(restored_pin->net == ids.gnd_net);
}

TEST_CASE("A DifferentialPair round-trips both members with polarity intact",
          "[connectivity][serialize]") {
  SampleIds ids;
  const ConnectivitySnapshot original = build_sample_snapshot(ids);
  const ConnectivitySnapshot restored = round_trip(original);

  const DifferentialPair* restored_pair = restored.table<DifferentialPair>().try_get(
      restored.table<DifferentialPair>().find(ids.diff_pair));
  REQUIRE(restored_pair != nullptr);
  REQUIRE(restored_pair->positive_net == ids.usb_p_net);
  REQUIRE(restored_pair->negative_net == ids.usb_n_net);
}

TEST_CASE("A Bus round-trips its member ordering", "[connectivity][serialize]") {
  SampleIds ids;
  const ConnectivitySnapshot original = build_sample_snapshot(ids);
  const ConnectivitySnapshot restored = round_trip(original);

  const Bus* restored_bus = restored.table<Bus>().try_get(restored.table<Bus>().find(ids.bus));
  REQUIRE(restored_bus != nullptr);
  REQUIRE(restored_bus->name == "D");
  REQUIRE(restored_bus->members.size() == 3);
  REQUIRE(restored_bus->members.at(0) == ids.usb_p_net);
  REQUIRE(restored_bus->members.at(1) == ids.usb_n_net);
  REQUIRE(restored_bus->members.at(2) == ids.gnd_net);
}

TEST_CASE("Entity ids are preserved, not reassigned, across the round trip",
          "[connectivity][serialize]") {
  SampleIds ids;
  const ConnectivitySnapshot original = build_sample_snapshot(ids);
  const ConnectivitySnapshot restored = round_trip(original);

  REQUIRE_FALSE(restored.table<Net>().find(ids.gnd_net).is_null());
  REQUIRE_FALSE(restored.table<Pin>().find(ids.pin).is_null());
}
