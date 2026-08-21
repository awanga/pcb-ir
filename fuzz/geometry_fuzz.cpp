// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/serialize.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// libFuzzer entry point (PCBIR_BUILD_FUZZ, Clang + -fsanitize=fuzzer only;
// see CONTRIBUTING.md). Feeds arbitrary bytes to the geometry
// deserializer -- the one place this module parses input it did not
// produce itself, and therefore the one place a malformed buffer could
// crash the process rather than fail cleanly.
// The exact name/signature libFuzzer's runtime looks for; not this
// project's usual snake_case convention, and not renameable.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::span<const uint8_t> buffer(data, size);
  try {
    pcbir::geometry::GeometryWorkspace workspace = pcbir::geometry::deserialize_geometry(buffer);
    // Exercise commit() too, not just deserialization itself.
    (void)workspace.commit();
    // deserialize_geometry verifies the buffer against schema corruption
    // before reading a field; a malformed buffer is expected to throw
    // pcbir::FormatError here, a clean, documented outcome rather than a
    // crash or out-of-bounds read.
    // NOLINTNEXTLINE(bugprone-empty-catch)
  } catch (...) {
  }
  return 0;
}
