// SPDX-License-Identifier: Apache-2.0
#include "pad_shape_extension.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "pad_shape.hpp"

namespace pcbir::kicad {

namespace {

constexpr std::size_t PAYLOAD_SIZE = 56;
constexpr std::size_t BYTES_PER_I64 = 8;

void put_i64_le(std::vector<uint8_t>& out, int64_t value) {
  const auto bits = static_cast<uint64_t>(value);
  for (std::size_t i = 0; i < BYTES_PER_I64; ++i) {
    out.push_back(static_cast<uint8_t>(bits >> (8 * i)));
  }
}

[[nodiscard]] int64_t get_i64_le(std::span<const uint8_t> bytes, std::size_t offset) {
  uint64_t bits = 0;
  for (std::size_t i = 0; i < BYTES_PER_I64; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    bits |= static_cast<uint64_t>(bytes[offset + i]) << (8 * i);
  }
  return static_cast<int64_t>(bits);
}

[[nodiscard]] std::optional<KicadPadShape> shape_from_byte(uint8_t value) {
  constexpr auto max_shape = static_cast<uint8_t>(KicadPadShape::Trapezoid);
  if (value > max_shape) {
    return std::nullopt;
  }
  return static_cast<KicadPadShape>(value);
}

} // namespace

std::vector<uint8_t> encode_pad_shape_extension(const PadShapeParams& params, int64_t rotation_e6) {
  std::vector<uint8_t> payload;
  payload.reserve(PAYLOAD_SIZE);

  payload.push_back(static_cast<uint8_t>(params.shape));
  for (int i = 0; i < 7; ++i) {
    payload.push_back(0); // reserved
  }
  put_i64_le(payload, params.width_nm);
  put_i64_le(payload, params.height_nm);
  put_i64_le(payload, params.roundrect_radius_nm);
  put_i64_le(payload, params.trapezoid_delta_x_nm);
  put_i64_le(payload, params.trapezoid_delta_y_nm);
  put_i64_le(payload, rotation_e6);

  return payload;
}

std::optional<DecodedPadShape> decode_pad_shape_extension(std::span<const uint8_t> payload) {
  if (payload.size() != PAYLOAD_SIZE) {
    return std::nullopt;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  const std::optional<KicadPadShape> shape = shape_from_byte(payload[0]);
  if (!shape.has_value()) {
    return std::nullopt;
  }

  DecodedPadShape result;
  result.params.shape = *shape;
  result.params.width_nm = get_i64_le(payload, 8);
  result.params.height_nm = get_i64_le(payload, 16);
  result.params.roundrect_radius_nm = get_i64_le(payload, 24);
  result.params.trapezoid_delta_x_nm = get_i64_le(payload, 32);
  result.params.trapezoid_delta_y_nm = get_i64_le(payload, 40);
  result.rotation_e6 = get_i64_le(payload, 48);
  return result;
}

} // namespace pcbir::kicad
