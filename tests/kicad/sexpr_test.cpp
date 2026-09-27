// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/sexpr.hpp"

#include <catch2/catch_test_macros.hpp>

using pcbir::kicad::parse_sexpr;
using pcbir::kicad::SExpr;
using pcbir::kicad::SExprParseError;

TEST_CASE("A bare symbol at the root parses as a Symbol node", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr("hello");
  REQUIRE(expr.is_symbol());
  REQUIRE(expr.text == "hello");
  REQUIRE(expr.children.empty());
}

TEST_CASE("A quoted string at the root parses as a String node", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr(R"("hello world")");
  REQUIRE(expr.is_string());
  REQUIRE(expr.text == "hello world");
}

TEST_CASE("An empty quoted string parses to an empty String node", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr(R"("")");
  REQUIRE(expr.is_string());
  REQUIRE(expr.text.empty());
}

TEST_CASE("A string literal unescapes \\\" and \\\\", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr(R"("a\"b\\c")");
  REQUIRE(expr.is_string());
  REQUIRE(expr.text == R"(a"b\c)");
}

TEST_CASE("A simple list parses its children in order", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr("(net 1 \"GND\")");
  REQUIRE(expr.is_list());
  REQUIRE(expr.children.size() == 3);
  REQUIRE(expr.children.at(0).is_symbol());
  REQUIRE(expr.children.at(0).text == "net");
  REQUIRE(expr.children.at(1).is_symbol());
  REQUIRE(expr.children.at(1).text == "1");
  REQUIRE(expr.children.at(2).is_string());
  REQUIRE(expr.children.at(2).text == "GND");
}

TEST_CASE("Nested lists parse to the correct tree shape", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr("(at 10 10 45)");
  REQUIRE(expr.is_list());
  REQUIRE(expr.children.size() == 4);

  const SExpr nested = parse_sexpr(R"(
    (footprint ""
      (layer "F.Cu")
      (at 10 10 45)
      (pad "1" smd rect (at -1 0) (size 1.6 1.2))
    )
  )");
  REQUIRE(nested.is_list());
  REQUIRE(nested.children.at(0).text == "footprint");
  REQUIRE(nested.children.at(1).is_string());
  REQUIRE(nested.children.at(1).text.empty());
  REQUIRE(nested.children.at(2).is_list());
  REQUIRE(nested.children.at(2).children.at(0).text == "layer");
  REQUIRE(nested.children.at(2).children.at(1).text == "F.Cu");
  const SExpr& pad = nested.children.at(4);
  REQUIRE(pad.is_list());
  REQUIRE(pad.children.at(0).text == "pad");
  REQUIRE(pad.children.at(1).text == "1");
  REQUIRE(pad.children.at(2).text == "smd");
  REQUIRE(pad.children.at(3).text == "rect");
}

TEST_CASE("An empty list parses with no children", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr("()");
  REQUIRE(expr.is_list());
  REQUIRE(expr.children.empty());
}

TEST_CASE("Leading and trailing whitespace around the root is ignored", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr("  \n\t(a b)\n  ");
  REQUIRE(expr.is_list());
  REQUIRE(expr.children.size() == 2);
}

TEST_CASE("Real pcbnew-formatted hex bitmask tokens parse as opaque symbols", "[kicad][sexpr]") {
  const SExpr expr = parse_sexpr("(layerselection 0x00000000_00000000_55555555_5755f5ff)");
  REQUIRE(expr.children.at(1).is_symbol());
  REQUIRE(expr.children.at(1).text == "0x00000000_00000000_55555555_5755f5ff");
}

TEST_CASE("Empty input is rejected", "[kicad][sexpr]") {
  REQUIRE_THROWS_AS(parse_sexpr(""), SExprParseError);
  REQUIRE_THROWS_AS(parse_sexpr("   "), SExprParseError);
}

TEST_CASE("An unterminated list is rejected", "[kicad][sexpr]") {
  REQUIRE_THROWS_AS(parse_sexpr("(a b"), SExprParseError);
  REQUIRE_THROWS_AS(parse_sexpr("(a (b)"), SExprParseError);
}

TEST_CASE("An unexpected closing paren is rejected", "[kicad][sexpr]") {
  REQUIRE_THROWS_AS(parse_sexpr(")"), SExprParseError);
  REQUIRE_THROWS_AS(parse_sexpr("(a))"), SExprParseError);
}

TEST_CASE("An unterminated string literal is rejected", "[kicad][sexpr]") {
  REQUIRE_THROWS_AS(parse_sexpr(R"("unterminated)"), SExprParseError);
  REQUIRE_THROWS_AS(parse_sexpr(R"((a "unterminated))"), SExprParseError);
}

TEST_CASE("Trailing content after the root expression is rejected", "[kicad][sexpr]") {
  REQUIRE_THROWS_AS(parse_sexpr("(a) (b)"), SExprParseError);
  REQUIRE_THROWS_AS(parse_sexpr("(a) garbage"), SExprParseError);
}

TEST_CASE("SExpr nodes compare structurally equal", "[kicad][sexpr]") {
  REQUIRE(parse_sexpr("(a b)") == parse_sexpr("(a b)"));
  REQUIRE_FALSE(parse_sexpr("(a b)") == parse_sexpr("(a c)"));
}
