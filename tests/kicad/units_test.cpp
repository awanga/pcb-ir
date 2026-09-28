// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/import.hpp"
#include "pcbir/kicad/units.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using pcbir::kicad::format_e6_to_degrees;
using pcbir::kicad::format_e6_to_ratio;
using pcbir::kicad::format_nm_to_mm;
using pcbir::kicad::ImportError;
using pcbir::kicad::parse_degrees_to_e6;
using pcbir::kicad::parse_mm_to_nm;
using pcbir::kicad::parse_ratio_to_e6;

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

TEST_CASE("format_nm_to_mm renders a whole-millimetre value with no decimal point",
          "[kicad][units]") {
  REQUIRE(format_nm_to_mm(5'000'000) == "5");
  REQUIRE(format_nm_to_mm(0) == "0");
}

TEST_CASE("format_nm_to_mm renders a fractional value, trimming trailing zeros", "[kicad][units]") {
  REQUIRE(format_nm_to_mm(1'600'000) == "1.6");
  REQUIRE(format_nm_to_mm(10'707'107) == "10.707107");
  REQUIRE(format_nm_to_mm(100'000) == "0.1");
}

TEST_CASE("format_nm_to_mm renders a negative value with a leading minus", "[kicad][units]") {
  REQUIRE(format_nm_to_mm(-1'600'000) == "-1.6");
  REQUIRE(format_nm_to_mm(-500'000) == "-0.5");
}

TEST_CASE("format_nm_to_mm round-trips through parse_mm_to_nm", "[kicad][units]") {
  for (const int64_t nm : {int64_t{0},
                           int64_t{5'000'000},
                           int64_t{-5'000'000},
                           int64_t{1'600'000},
                           int64_t{10'707'107},
                           int64_t{-1},
                           int64_t{1}}) {
    REQUIRE(parse_mm_to_nm(format_nm_to_mm(nm)) == nm);
  }
}

TEST_CASE("format_e6_to_degrees shares format_nm_to_mm's exact conversion", "[kicad][units]") {
  REQUIRE(format_e6_to_degrees(90'000'000) == "90");
  REQUIRE(format_e6_to_degrees(-45'500'000) == "-45.5");
  REQUIRE(parse_degrees_to_e6(format_e6_to_degrees(12'345'678)) == 12'345'678);
}

TEST_CASE("format_e6_to_ratio shares format_nm_to_mm's exact conversion", "[kicad][units]") {
  REQUIRE(format_e6_to_ratio(250'000) == "0.25");
  REQUIRE(parse_ratio_to_e6(format_e6_to_ratio(250'000)) == 250'000);
}
