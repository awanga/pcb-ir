// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/bus.hpp"
#include "pcbir/connectivity/differential_pair.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/pin.hpp"
#include "pcbir/core/arena.hpp"
#include "pcbir/core/entity_id.hpp"

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

using pcbir::connectivity::Bus;
using pcbir::connectivity::DifferentialPair;
using pcbir::connectivity::Net;
using pcbir::connectivity::Pin;
using pcbir::core::Arena;
using pcbir::core::EntityId;

TEST_CASE("Net carries only a name, independent of geometry", "[connectivity][entities]") {
  const Net net{.name = "USB_D+"};

  REQUIRE(net.name == "USB_D+");
}

TEST_CASE("Pin ties a pad to a net by stable EntityId", "[connectivity][entities]") {
  const Pin pin{.pad = EntityId{1}, .net = EntityId{2}};

  REQUIRE(pin.pad == EntityId{1});
  REQUIRE(pin.net == EntityId{2});
}

TEST_CASE("A default-constructed Pin has a null pad and net", "[connectivity][entities]") {
  const Pin pin{};

  REQUIRE(pin.pad.is_null());
  REQUIRE(pin.net.is_null());
}

TEST_CASE("DifferentialPair records which net is positive and which is negative",
          "[connectivity][entities]") {
  const DifferentialPair pair{.positive_net = EntityId{10}, .negative_net = EntityId{11}};

  REQUIRE(pair.positive_net == EntityId{10});
  REQUIRE(pair.negative_net == EntityId{11});
  REQUIRE(pair.positive_net != pair.negative_net);
}

TEST_CASE("Bus preserves member ordering", "[connectivity][entities]") {
  const Bus bus{.name = "D", .members = {EntityId{1}, EntityId{2}, EntityId{3}}};

  REQUIRE(bus.name == "D");
  REQUIRE(bus.members.size() == 3);
  REQUIRE(bus.members.at(0) == EntityId{1});
  REQUIRE(bus.members.at(1) == EntityId{2});
  REQUIRE(bus.members.at(2) == EntityId{3});
}

TEST_CASE("Connectivity entities are distinct component-table types", "[connectivity][entities]") {
  Arena<Net> nets;
  Arena<Pin> pins;

  const auto net_handle = nets.insert(Net{.name = "GND"}, EntityId{1});
  const auto pin_handle = pins.insert(Pin{.pad = EntityId{2}, .net = EntityId{1}}, EntityId{3});

  REQUIRE(nets.get(net_handle).name == "GND");
  REQUIRE(pins.get(pin_handle).net == EntityId{1});
  static_assert(!std::is_same_v<decltype(net_handle), decltype(pin_handle)>);
}
