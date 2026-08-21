// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_FORMAT_VERSION_HPP
#define PCBIR_FORMAT_VERSION_HPP

#include <cstdint>

namespace pcbir {

// The (major, minor) wire-format version carried by every BoardSnapshot
// (docs/format-spec.md -- Versioning policy). Distinct from
// core::SnapshotVersion, which tracks commits within one snapshot lineage,
// not the wire format itself.
struct FormatVersion {
  uint32_t major = 0;
  uint32_t minor = 0;

  friend constexpr bool operator==(const FormatVersion&, const FormatVersion&) = default;
};

// The version this build of PCB-IR writes, and the major version it
// requires on read (docs/format-spec.md -- Versioning policy: a reader
// rejects an unknown major, tolerates an unknown minor).
inline constexpr FormatVersion CURRENT_FORMAT_VERSION{.major = 1, .minor = 0};

} // namespace pcbir

#endif // PCBIR_FORMAT_VERSION_HPP
