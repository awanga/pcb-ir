// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_SERIALIZE_HPP
#define PCBIR_GEOMETRY_SERIALIZE_HPP

#include "pcbir/core/snapshot.hpp"
#include "pcbir/core/workspace.hpp"
#include "pcbir/geometry/board_outline.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/drill_hit.hpp"
#include "pcbir/geometry/footprint.hpp"
#include "pcbir/geometry/keepout.hpp"
#include "pcbir/geometry/mask_opening.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/silkscreen_graphic.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace pcbir::geometry {

// The geometry layer's own snapshot/workspace, composing every typed board
// entity as its own component table (docs/architecture.md). This is
// composed into the full board snapshot in a later phase; standalone here
// so the geometry layer round-trips and is testable on its own
// (schemas/geometry.fbs).
using GeometrySnapshot = core::Snapshot<Pad,
                                        Via,
                                        Track,
                                        CopperPour,
                                        Keepout,
                                        DrillHit,
                                        MaskOpening,
                                        SilkscreenGraphic,
                                        Footprint,
                                        BoardOutline>;
using GeometryWorkspace = core::Workspace<Pad,
                                          Via,
                                          Track,
                                          CopperPour,
                                          Keepout,
                                          DrillHit,
                                          MaskOpening,
                                          SilkscreenGraphic,
                                          Footprint,
                                          BoardOutline>;

// Serializes `snapshot` to a FlatBuffers buffer per schemas/geometry.fbs.
// No FlatBuffers type appears in this signature -- the generated headers
// are an implementation detail of serialize.cpp only.
[[nodiscard]] std::vector<uint8_t> serialize(const GeometrySnapshot& snapshot);

// Deserializes a buffer produced by serialize() back into a workspace with
// every entity's original EntityId preserved (via
// Workspace::insert_with_id). Returns a Workspace rather than a Snapshot
// because the only way to obtain a Snapshot is Workspace::commit()
// (docs/architecture.md); call commit() on the result to get one. The
// buffer is verified against schema corruption before any field is read
// (docs/format-spec.md -- Serialization fuzz target); a structurally
// invalid buffer throws pcbir::FormatError rather than crashing or
// reading out of bounds.
[[nodiscard]] GeometryWorkspace deserialize_geometry(std::span<const uint8_t> buffer);

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_SERIALIZE_HPP
