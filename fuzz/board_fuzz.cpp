// SPDX-License-Identifier: Apache-2.0
#include "pcbir/serialize.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// libFuzzer entry point (PCBIR_BUILD_FUZZ, Clang + -fsanitize=fuzzer only;
// see CONTRIBUTING.md). Feeds arbitrary bytes to the composed board
// deserializer (schemas/snapshot.fbs) -- the one deserializer that also
// has to reject an unsupported format major version, not just a
// structurally corrupt buffer.
// The exact name/signature libFuzzer's runtime looks for; not this
// project's usual snake_case convention, and not renameable.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::span<const uint8_t> buffer(data, size);
  try {
    (void)pcbir::deserialize_board(buffer);
    // NOLINTNEXTLINE(bugprone-empty-catch)
  } catch (...) {
  }
  return 0;
}
