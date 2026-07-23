# PCB-IR Architecture

> Status: stub.

## Overview

PCB-IR represents a PCB as a persistent semantic physical-design graph across explicit
layers: geometry, connectivity, constraints, stackup, and process/optimization metadata.

## Snapshots and workspaces

- **Immutable snapshot** — the canonical state, backed by a zero-copy, mmap-able FlatBuffers
  buffer. Read-only; no mutation API.
- **Transactional workspace** — a separate mutable overlay. Edits accumulate in the workspace
  and a `commit` produces a new immutable snapshot. The base snapshot is never modified;
  structural sharing is used where possible.

This separation exists because FlatBuffers buffers are immutable once built.

## Memory model

- Arena allocation for snapshot-owned objects; typed, generation-checked handles instead of
  raw pointers.
- ECS-like composition: an entity is a typed handle; its data lives in separate typed
  component tables (geometry, connectivity, stackup reference, metadata), not a deep class
  hierarchy. This keeps layers independently extensible and cache-friendly.

## Identity & versioning

- **`EntityId`** — a stable, monotonically assigned 64-bit identity for a logical entity. It
  is never reused and is carried across every future commit for that entity's lifetime; it is
  what the serialized schema and cross-layer references (e.g. a via naming a stackup layer)
  use. Distinct from a `Handle`, which is an arena-slot reference.
- **`Handle<Tag>`** — an arena-local, generation-checked (index, generation) pair used for
  fast in-memory lookup and use-after-erase detection. A handle is not guaranteed to survive
  arena reorganizations (e.g. slot reuse after an erase) the way an `EntityId` is; call
  `Arena::find(EntityId)` to recover a fresh handle when needed.
- **`SnapshotVersion`** — a monotonically increasing counter on the snapshot lineage,
  incremented by one on every `Workspace::commit()`.

## Determinism

No floating point in serialized output. Canonical element ordering and stable iteration are
invariants verified by a determinism test suite. Building the same logical board
twice must yield byte-identical snapshots.

## Layering

| Layer | Owns | Independent of |
|---|---|---|
| Geometry | shapes, primitives | connectivity |
| Connectivity | nets, pins, diff pairs, buses | geometry |
| Stackup | materials, layers, vias | — |
| Metadata | provenance, process, ML features | — |

"Manufacturing intent" and "electrical intent" are not separate top-level layers — they are
expressed through the layers above: manufacturing intent spans Stackup (process capability)
and the Constraint System (DFM/assembly rules, Post-MVP); electrical intent spans
Connectivity (diff pairs, buses) and the Constraint System (impedance/timing/skew rules,
Post-MVP).

## Extensibility mechanism

Two schema-level escape hatches exist from v0.1 onward so future/esoteric cases (RF
metadata, flex-PCB, embedded components, chiplets/interposers, photonics) never require a
breaking schema change:

- **Namespaced typed-extension table** (`VENDOR_*`/`EXT_*`/`PCBIR_*`, see
  `docs/extensions-governance.md`) — attachable to any entity; unknown extensions are
  safely ignored by readers that don't understand them.
- **Opaque passthrough blob** — per-entity raw bytes for source-format data an importer
  recognizes syntactically but can't semantically map. Distinct from the typed-extension
  table: this is un-interpreted, round-tripped verbatim.

## Archive / container layer (reserved, Post-MVP)

The FlatBuffers snapshot above is a single immutable buffer — it has no native notion of
version history, deltas, compression, or embedded blobs. A Post-MVP chunked container is
designed here now so v0.1's schema never needs a breaking change to accommodate it:

- **Format:** glTF-`.glb`-style — a small fixed header + a table-of-contents + a sequence of
  typed chunks (`FullSnapshot`, `Delta`, `Blob`, `Provenance`). Chosen over reusing Zip or
  SQLite as the container: simpler, fully deterministic, no extra runtime dependency beyond
  a compressor, and every chunk type maps directly onto a concept this project already has.
- **Compression:** zstd per chunk (permissive-licensed, fast, ubiquitous).
- **Deltas:** a `Delta` chunk is a *serialized workspace transaction log* (the same edit
  representation the mutable workspace already produces on commit) — not a binary diff of
  two FlatBuffers buffers, which wouldn't be semantically meaningful. This makes the archive
  layer a natural persistence of history that already exists in the runtime model, not a new
  concept.
- **Blobs / provenance:** embedded original source-format bytes (e.g. the imported
  `.kicad_pcb`) and a provenance chunk recording import/transform history, so "no
  information lost" can be verified against the original file even after PCB-IR round-trips.

(To be expanded.)
