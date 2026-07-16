# PCB-IR File Format Specification

> Status: stub. This document must be sufficient for an independent reader implementation.

## Goals

- Vendor-neutral, versioned, deterministic on-disk representation.
- Zero-copy load (mmap → read without full deserialize).
- The file format is the primary integration surface alongside the C ABI.

## Encoding

- FlatBuffers. Schemas live in `schemas/*.fbs`.
- No floating-point values on the wire. Coordinates are int64 nanometers.

## Versioning policy

- Each file carries a `(major, minor)` wire-format version.
- A reader **rejects** unknown `major`; **tolerates** unknown `minor` (additive fields only).
- FlatBuffers provides field-addition compatibility but NOT structural migration. A
  documented migration hook is reserved before v0.1 freeze.
- Any schema change is API-breaking by default and is user-gated.

## Canonicalization (determinism)

- Canonical element ordering (to be specified per table).
- Stable iteration; no hash-iteration-order leakage into bytes.

## Geometry encoding

- Segments and arcs are stored symbolically and exactly (arc = endpoints + center/sweep).
- Splines/teardrops are flattened deterministically to arcs/segments on import; the
  flattening tolerance is fixed and documented here. Flattening is lossy by design.

## Extensions

- Namespaced; see `docs/extensions-governance.md`.

(To be expanded with per-table layouts and golden byte examples.)
