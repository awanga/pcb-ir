// SPDX-License-Identifier: Apache-2.0
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/format_error.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/serialize.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::deserialize_board;
using pcbir::FormatError;
using pcbir::connectivity::deserialize_connectivity;
using pcbir::geometry::deserialize_geometry;
using pcbir::stackup::deserialize_stackup;

namespace {
constexpr std::array<uint8_t, 16> GARBAGE_BUFFER = {
    0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x20, 0x30, 0x40};
} // namespace

// Every deserializer runs FlatBuffers structural verification before
// reading a single field (docs/format-spec.md -- Serialization fuzz
// target); a random/corrupt buffer must be a clean, catchable FormatError
// rather than a crash or out-of-bounds read.
TEST_CASE("Deserializing a random garbage buffer throws FormatError, never crashes",
          "[serialize][format-error]") {
  REQUIRE_THROWS_AS(deserialize_geometry(GARBAGE_BUFFER), FormatError);
  REQUIRE_THROWS_AS(deserialize_connectivity(GARBAGE_BUFFER), FormatError);
  REQUIRE_THROWS_AS(deserialize_stackup(GARBAGE_BUFFER), FormatError);
  REQUIRE_THROWS_AS(deserialize_board(GARBAGE_BUFFER), FormatError);
}

TEST_CASE("Deserializing an empty buffer throws FormatError, never crashes",
          "[serialize][format-error]") {
  const std::vector<uint8_t> empty_buffer;
  REQUIRE_THROWS_AS(deserialize_geometry(empty_buffer), FormatError);
  REQUIRE_THROWS_AS(deserialize_connectivity(empty_buffer), FormatError);
  REQUIRE_THROWS_AS(deserialize_stackup(empty_buffer), FormatError);
  REQUIRE_THROWS_AS(deserialize_board(empty_buffer), FormatError);
}

TEST_CASE("Deserializing a truncated valid buffer throws FormatError, never crashes",
          "[serialize][format-error]") {
  // A truncated buffer still starts with a plausible root offset, so this
  // exercises the Verifier's bounds checking rather than just an
  // immediately-malformed header.
  const std::vector<uint8_t> truncated = {0x04, 0x00, 0x00, 0x00};
  REQUIRE_THROWS_AS(deserialize_geometry(truncated), FormatError);
  REQUIRE_THROWS_AS(deserialize_board(truncated), FormatError);
}
