# PCB-IR File Format Specification

> Status: stub. This document must be sufficient for an independent reader implementation.

## Goals

- Vendor-neutral, versioned, deterministic on-disk representation.
- Zero-copy load (mmap → read without full deserialize).
- The file format is the primary integration surface alongside the C ABI.

## Encoding

- FlatBuffers. Schemas live in `schemas/*.fbs`.
- No floating-point values on the wire. Coordinates are int64 nanometers, giving a
  representable range of roughly ±9.2e9 meters (the full `int64_t` range) -- vastly beyond
  any real board, but the honest bound of the type. Arithmetic that would overflow this range
  is a checked condition (`pcbir::geometry::checked_add`/`checked_sub`/`checked_mul`), not
  silent wraparound.

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
- Splines/teardrops (cubic Beziers) are flattened deterministically to segments on import,
  via a **fixed** number of De Casteljau bisections (`BEZIER_FLATTEN_DEPTH = 10`,
  `pcbir::geometry::flatten_cubic_bezier`) rather than an adaptive, threshold-based
  algorithm. This is deliberate: bisection at t=1/2 is pure integer add + divide-by-2, so the
  result is bit-for-bit identical across platforms, whereas an adaptive flatness test would
  need either wide (>64-bit) integer comparisons or floating point, both of which risk
  cross-platform divergence (FMA contraction, extended-precision registers) that would break
  the byte-identical-snapshot invariant. A cubic Bezier's control-polygon deviation from the
  true curve shrinks by at least 4x per bisection, so depth `n` bounds the worst-case
  flattening error at `(initial control-polygon deviation) / 4^n`. At `n = 10` (1024
  segments per curve), even a curve whose control points span the full documented coordinate
  range (~9.2e9 m) flattens to within about 1 micrometer of the true curve; any real
  board-scale curve (mm–cm control polygons) flattens far tighter. Flattening is lossy by
  design; the original curve is not recoverable from the flattened segments.

## Extensions

- Namespaced; see `docs/extensions-governance.md`.

(To be expanded with per-table layouts and golden byte examples.)
