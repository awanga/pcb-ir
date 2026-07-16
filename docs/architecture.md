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

(To be expanded.)
