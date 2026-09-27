// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/units.hpp"

#include <catch2/catch_test_macros.hpp>

using pcbir::kicad::ImportError;
using pcbir::kicad::parse_mm_to_nm;

TEST_CASE("parse_mm_to_nm converts a whole-millimetre value exactly", "[kicad][units]") {
  REQUIRE(parse_mm_to_nm("5") == 5'000'000);
  REQUIRE(parse_mm_to_nm("0") == 0);
}

TEST_CASE("parse_mm_to_nm converts a fractional-millimetre value exactly", "[kicad][units]") {
  REQUIRE(parse_mm_to_nm("1.6") == 1'600'000);
  REQUIRE(parse_mm_to_nm("10.707107") == 10'707'107);
  REQUIRE(parse_mm_to_nm("0.1") == 100'000);
}

TEST_CASE("parse_mm_to_nm handles a leading sign", "[kicad][units]") {
  REQUIRE(parse_mm_to_nm("-1.6") == -1'600'000);
  REQUIRE(parse_mm_to_nm("+1.6") == 1'600'000);
  REQUIRE(parse_mm_to_nm("-0.5") == -500'000);
}

TEST_CASE("parse_mm_to_nm pads a short fractional part with zeros", "[kicad][units]") {
  REQUIRE(parse_mm_to_nm("1.5") == 1'500'000);
  REQUIRE(parse_mm_to_nm("1.05") == 1'050'000);
}

TEST_CASE("parse_mm_to_nm rounds a 7th+ fractional digit half away from zero", "[kicad][units]") {
  REQUIRE(parse_mm_to_nm("1.0000005") == 1'000'001);   // exactly half, rounds up
  REQUIRE(parse_mm_to_nm("1.0000004") == 1'000'000);   // rounds down
  REQUIRE(parse_mm_to_nm("1.9999995") == 2'000'000);   // carries into the mm digit
  REQUIRE(parse_mm_to_nm("-1.0000005") == -1'000'001); // negative rounds away from zero
}

TEST_CASE("parse_mm_to_nm accepts a bare fractional or trailing-dot value", "[kicad][units]") {
  REQUIRE(parse_mm_to_nm(".5") == 500'000);
  REQUIRE(parse_mm_to_nm("5.") == 5'000'000);
}

TEST_CASE("parse_mm_to_nm rejects malformed input", "[kicad][units]") {
  REQUIRE_THROWS_AS(parse_mm_to_nm(""), ImportError);
  REQUIRE_THROWS_AS(parse_mm_to_nm("-"), ImportError);
  REQUIRE_THROWS_AS(parse_mm_to_nm("."), ImportError);
  REQUIRE_THROWS_AS(parse_mm_to_nm("1.2.3"), ImportError);
  REQUIRE_THROWS_AS(parse_mm_to_nm("1mm"), ImportError);
  REQUIRE_THROWS_AS(parse_mm_to_nm("abc"), ImportError);
}

TEST_CASE("parse_mm_to_nm rejects a value that overflows int64_t nanometres", "[kicad][units]") {
  REQUIRE_THROWS_AS(parse_mm_to_nm("99999999999999999999999999"), ImportError);
}
