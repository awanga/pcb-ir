// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_UUID_HPP
#define PCBIR_KICAD_UUID_HPP

#include <string>
#include <string_view>

// UUIDv5 generation for the KiCad exporter's `(uuid "...")` fields
// (docs/rfcs/0003-kicad-importer-exporter.md's "UUID/tstamp generation";
// docs/format-spec.md's "KiCad export UUID namespace"). Not part of the
// public API -- used only by export.cpp -- so this lives under src/,
// mirroring pad_shape.hpp's own precedent for an importer/exporter-internal
// helper.
namespace pcbir::kicad {

// PCB-IR's fixed UUIDv5 namespace (docs/format-spec.md's "KiCad export UUID
// namespace" -- generated once, committed, never changed: changing it would
// change every future export's UUIDs).
inline constexpr std::string_view UUID_NAMESPACE = "4183b33a-de9e-4074-bdb1-e8d33c7cc35a";

// Computes a UUIDv5 (RFC 4122 section 4.3, name-based, SHA-1) from
// `namespace_uuid` (a standard "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
// string) and `name`, returning the result in that same
// lowercase-hyphenated string form. A pure function of its two inputs --
// the same (namespace, name) pair always produces the same UUID -- so
// callers get a reproducible, non-random identifier for a given entity
// identity, satisfying this project's determinism invariant
// (docs/rfcs/0003-kicad-importer-exporter.md).
//
// Throws std::invalid_argument if `namespace_uuid` isn't a well-formed
// 36-character UUID string.
[[nodiscard]] std::string uuid_v5(std::string_view namespace_uuid, std::string_view name);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_UUID_HPP
