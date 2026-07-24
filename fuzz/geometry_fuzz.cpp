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
    // deserialize_geometry does not yet verify the buffer against schema
    // corruption (see its own doc comment in serialize.hpp) -- a thrown
    // exception here is an acceptable outcome for this target. Making
    // corrupt input fail *without* an exception/crash at all is Phase 5's
    // "Serialization fuzz target" task (TASKS.md), not this one's.
    // NOLINTNEXTLINE(bugprone-empty-catch)
  } catch (...) {
  }
  return 0;
}
