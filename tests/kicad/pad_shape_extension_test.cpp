// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/pad_shape.hpp"
#include "pcbir/kicad/pad_shape_extension.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::kicad::decode_pad_shape_extension;
using pcbir::kicad::DecodedPadShape;
using pcbir::kicad::encode_pad_shape_extension;
using pcbir::kicad::KicadPadShape;
using pcbir::kicad::PadShapeParams;

namespace {

// REQUIREs `opt` is present and returns its value -- lets every call site
// below dereference the result of decode_pad_shape_extension without
// tripping bugprone-unchecked-optional-access (clang-tidy doesn't treat
// Catch2's REQUIRE(opt.has_value()) macro as narrowing `opt`'s type, so
// each dereference downstream still looks unchecked to it; centralizing
// the one real check plus its NOLINT here keeps every other line clean).
[[nodiscard]] DecodedPadShape require_decoded(const std::optional<DecodedPadShape>& opt) {
  REQUIRE(opt.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  return *opt;
}

} // namespace

TEST_CASE("encode_pad_shape_extension/decode_pad_shape_extension round-trip every shape kind",
          "[kicad][export]") {
  const PadShapeParams params{.shape = KicadPadShape::RoundRect,
                              .width_nm = 2'000'000,
                              .height_nm = 1'000'000,
                              .roundrect_radius_nm = 250'000,
                              .trapezoid_delta_x_nm = 0,
                              .trapezoid_delta_y_nm = 0};
  constexpr int64_t rotation_e6 = 45'000'000;

  const std::vector<uint8_t> payload = encode_pad_shape_extension(params, rotation_e6);
  const DecodedPadShape decoded = require_decoded(decode_pad_shape_extension(payload));

  REQUIRE(decoded.params.shape == params.shape);
  REQUIRE(decoded.params.width_nm == params.width_nm);
  REQUIRE(decoded.params.height_nm == params.height_nm);
  REQUIRE(decoded.params.roundrect_radius_nm == params.roundrect_radius_nm);
  REQUIRE(decoded.params.trapezoid_delta_x_nm == params.trapezoid_delta_x_nm);
  REQUIRE(decoded.params.trapezoid_delta_y_nm == params.trapezoid_delta_y_nm);
  REQUIRE(decoded.rotation_e6 == rotation_e6);
}

TEST_CASE("encode_pad_shape_extension/decode_pad_shape_extension round-trip negative rotations "
          "and trapezoid deltas",
          "[kicad][export]") {
  const PadShapeParams params{.shape = KicadPadShape::Trapezoid,
                              .width_nm = 1'600'000,
                              .height_nm = 1'200'000,
                              .roundrect_radius_nm = 0,
                              .trapezoid_delta_x_nm = -300'000,
                              .trapezoid_delta_y_nm = 0};
  constexpr int64_t rotation_e6 = -90'000'000;

  const DecodedPadShape decoded =
      require_decoded(decode_pad_shape_extension(encode_pad_shape_extension(params, rotation_e6)));

  REQUIRE(decoded.params.shape == KicadPadShape::Trapezoid);
  REQUIRE(decoded.params.trapezoid_delta_x_nm == -300'000);
  REQUIRE(decoded.rotation_e6 == -90'000'000);
}

TEST_CASE("decode_pad_shape_extension rejects a payload of the wrong size", "[kicad][export]") {
  const std::vector<uint8_t> too_short(10, 0);
  REQUIRE_FALSE(decode_pad_shape_extension(too_short).has_value());

  const std::vector<uint8_t> too_long(100, 0);
  REQUIRE_FALSE(decode_pad_shape_extension(too_long).has_value());
}

TEST_CASE("decode_pad_shape_extension rejects an unrecognized shape byte", "[kicad][export]") {
  std::vector<uint8_t> payload =
      encode_pad_shape_extension(PadShapeParams{.shape = KicadPadShape::Rect,
                                                .width_nm = 1,
                                                .height_nm = 1,
                                                .roundrect_radius_nm = 0,
                                                .trapezoid_delta_x_nm = 0,
                                                .trapezoid_delta_y_nm = 0},
                                 0);
  payload.at(0) = 200; // not a valid KicadPadShape value
  REQUIRE_FALSE(decode_pad_shape_extension(payload).has_value());
}
