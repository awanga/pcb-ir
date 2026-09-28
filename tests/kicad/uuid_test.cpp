// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/uuid.hpp"

#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>

using pcbir::kicad::UUID_NAMESPACE;
using pcbir::kicad::uuid_v5;

namespace {
// RFC 4122 defines four well-known namespace UUIDs; Python's standard
// `uuid` module implements the same UUIDv5 algorithm this file tests
// against, so its output for these namespaces is an independent
// known-answer check, not a value this codebase invented.
constexpr const char* NAMESPACE_DNS = "6ba7b810-9dad-11d1-80b4-00c04fd430c8";
constexpr const char* NAMESPACE_URL = "6ba7b811-9dad-11d1-80b4-00c04fd430c8";
} // namespace

TEST_CASE("uuid_v5 matches Python's uuid.uuid5 for NAMESPACE_DNS", "[kicad][uuid]") {
  REQUIRE(uuid_v5(NAMESPACE_DNS, "python.org") == "886313e1-3b8a-5372-9b90-0c9aee199e5d");
}

TEST_CASE("uuid_v5 matches Python's uuid.uuid5 for NAMESPACE_URL", "[kicad][uuid]") {
  REQUIRE(uuid_v5(NAMESPACE_URL, "python.org") == "7af94e2b-4dd9-50f0-9c9a-8a48519bdef0");
}

TEST_CASE("uuid_v5 matches Python's uuid.uuid5 for PCB-IR's own namespace", "[kicad][uuid]") {
  REQUIRE(uuid_v5(UUID_NAMESPACE, "pcbir:geometry:pad:42") ==
          "e763ce36-83a5-5ea5-a043-35fe7677a6a2");
  REQUIRE(uuid_v5(UUID_NAMESPACE, "") == "71aec8ee-9380-5516-88fd-fc2400560acf");
}

TEST_CASE("uuid_v5 sets the version 5 and RFC 4122 variant bits", "[kicad][uuid]") {
  const std::string id = uuid_v5(UUID_NAMESPACE, "pcbir:geometry:pad:1");
  REQUIRE(id.size() == 36);
  REQUIRE(id.at(14) == '5');
  REQUIRE((id.at(19) == '8' || id.at(19) == '9' || id.at(19) == 'a' || id.at(19) == 'b'));
}

TEST_CASE("uuid_v5 is a pure, deterministic function of its inputs", "[kicad][uuid]") {
  REQUIRE(uuid_v5(UUID_NAMESPACE, "pcbir:geometry:pad:1") ==
          uuid_v5(UUID_NAMESPACE, "pcbir:geometry:pad:1"));
  REQUIRE(uuid_v5(UUID_NAMESPACE, "pcbir:geometry:pad:1") !=
          uuid_v5(UUID_NAMESPACE, "pcbir:geometry:pad:2"));
}

TEST_CASE("uuid_v5 rejects a malformed namespace UUID", "[kicad][uuid]") {
  REQUIRE_THROWS_AS(uuid_v5("not-a-uuid", "x"), std::invalid_argument);
}
