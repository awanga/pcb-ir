// SPDX-License-Identifier: Apache-2.0
#include "uuid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pcbir::kicad {

namespace {

// Minimal SHA-1 (RFC 3174) -- exactly what UUIDv5 (RFC 4122 section 4.3)
// requires and nothing more; not a general-purpose cryptographic hashing
// facility, and not used anywhere a security boundary depends on it
// (UUIDv5 namespace hashing is a determinism/identifier-derivation
// mechanism, not authentication). Implementing this directly (~80 lines)
// avoids a new dependency for something this small
// (docs/rfcs/0003-kicad-importer-exporter.md).
class Sha1 {
public:
  void update(std::span<const uint8_t> data) {
    for (const uint8_t byte : data) {
      buffer_.at(buffer_len_++) = byte;
      ++total_len_;
      if (buffer_len_ == buffer_.size()) {
        process_block();
        buffer_len_ = 0;
      }
    }
  }

  [[nodiscard]] std::array<uint8_t, 20> finish() {
    const uint64_t bit_len = total_len_ * 8;
    update(std::array<uint8_t, 1>{0x80});
    while (buffer_len_ != 56) {
      update(std::array<uint8_t, 1>{0x00});
    }
    std::array<uint8_t, 8> len_bytes{};
    for (std::size_t i = 0; i < len_bytes.size(); ++i) {
      len_bytes.at(i) = static_cast<uint8_t>(bit_len >> (8 * (len_bytes.size() - 1 - i)));
    }
    update(len_bytes);

    std::array<uint8_t, 20> digest{};
    for (std::size_t word = 0; word < h_.size(); ++word) {
      digest.at((word * 4) + 0) = static_cast<uint8_t>(h_.at(word) >> 24);
      digest.at((word * 4) + 1) = static_cast<uint8_t>(h_.at(word) >> 16);
      digest.at((word * 4) + 2) = static_cast<uint8_t>(h_.at(word) >> 8);
      digest.at((word * 4) + 3) = static_cast<uint8_t>(h_.at(word));
    }
    return digest;
  }

private:
  [[nodiscard]] static uint32_t rotl(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
  }

  void process_block() {
    std::array<uint32_t, 80> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w.at(i) = (static_cast<uint32_t>(buffer_.at(i * 4)) << 24) |
                (static_cast<uint32_t>(buffer_.at((i * 4) + 1)) << 16) |
                (static_cast<uint32_t>(buffer_.at((i * 4) + 2)) << 8) |
                static_cast<uint32_t>(buffer_.at((i * 4) + 3));
    }
    for (std::size_t i = 16; i < 80; ++i) {
      w.at(i) = rotl(w.at(i - 3) ^ w.at(i - 8) ^ w.at(i - 14) ^ w.at(i - 16), 1);
    }

    uint32_t a = h_.at(0);
    uint32_t b = h_.at(1);
    uint32_t c = h_.at(2);
    uint32_t d = h_.at(3);
    uint32_t e = h_.at(4);
    for (std::size_t i = 0; i < 80; ++i) {
      uint32_t f = 0;
      uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const uint32_t temp = rotl(a, 5) + f + e + k + w.at(i);
      e = d;
      d = c;
      c = rotl(b, 30);
      b = a;
      a = temp;
    }
    h_.at(0) += a;
    h_.at(1) += b;
    h_.at(2) += c;
    h_.at(3) += d;
    h_.at(4) += e;
  }

  std::array<uint32_t, 5> h_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::array<uint8_t, 64> buffer_{};
  std::size_t buffer_len_ = 0;
  uint64_t total_len_ = 0;
};

[[nodiscard]] uint8_t hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(c - 'A' + 10);
  }
  throw std::invalid_argument("uuid_v5: malformed namespace UUID (non-hex digit)");
}

[[nodiscard]] std::array<uint8_t, 16> parse_uuid(std::string_view text) {
  std::string hex;
  hex.reserve(32);
  for (const char c : text) {
    if (c != '-') {
      hex.push_back(c);
    }
  }
  if (hex.size() != 32) {
    throw std::invalid_argument("uuid_v5: malformed namespace UUID (expected 32 hex digits)");
  }
  std::array<uint8_t, 16> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes.at(i) =
        static_cast<uint8_t>((hex_nibble(hex.at(i * 2)) << 4) | hex_nibble(hex.at((i * 2) + 1)));
  }
  return bytes;
}

[[nodiscard]] std::string format_uuid(const std::array<uint8_t, 16>& bytes) {
  static constexpr std::array<char, 16> hex_digits = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string out;
  out.reserve(36);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    out.push_back(hex_digits.at(bytes.at(i) >> 4));
    out.push_back(hex_digits.at(bytes.at(i) & 0x0F));
    if (i == 3 || i == 5 || i == 7 || i == 9) {
      out.push_back('-');
    }
  }
  return out;
}

} // namespace

// Both parameters are semantically distinct (a fixed namespace vs. a
// per-entity name), not interchangeable; precedented by
// contour.cpp/handle.hpp's own use of this suppression for the same
// reason.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::string uuid_v5(std::string_view namespace_uuid, std::string_view name) {
  const std::array<uint8_t, 16> namespace_bytes = parse_uuid(namespace_uuid);

  Sha1 hasher;
  hasher.update(namespace_bytes);
  // A byte view of `name`'s existing character storage, required by RFC
  // 4122's UUIDv5 algorithm (hash namespace bytes || name bytes) --
  // std::span's element type has no relation to `char`'s signedness, so
  // this is the same byte-reinterpretation pcbir_c_abi's write_file()
  // already does for the same reason.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  hasher.update(std::span{reinterpret_cast<const uint8_t*>(name.data()), name.size()});
  const std::array<uint8_t, 20> digest = hasher.finish();

  std::array<uint8_t, 16> result{};
  for (std::size_t i = 0; i < result.size(); ++i) {
    result.at(i) = digest.at(i);
  }
  result.at(6) = static_cast<uint8_t>((result.at(6) & 0x0F) | 0x50); // Version 5.
  result.at(8) = static_cast<uint8_t>((result.at(8) & 0x3F) | 0x80); // Variant RFC 4122.
  return format_uuid(result);
}

} // namespace pcbir::kicad
