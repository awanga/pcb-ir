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
- A polygon (`pcbir::geometry::Polygon`) is an outline plus zero or more holes, each a closed
  loop (`Contour`) of segment/arc spans. **Self-intersection and orientation are both defined
  over each span's chord** (an arc's straight start-end line, not its true curved shape), not
  the arc's true swept shape: two circles generically intersect at irrational coordinates, so
  exact arc-arc intersection *existence* cannot be decided in integer arithmetic the way
  segment-segment crossing can (via orientation predicates alone, with no need to compute the
  intersection point itself). This is consistent with Clipper2 already flattening arcs at the
  boolean-op boundary. Winding convention: outline is CounterClockwise, holes are Clockwise.
  Hole-in-outline containment is not yet checked (deferred to the broader validation pass).

## Boolean operations (Clipper2 boundary)

- `pcbir::geometry::boolean_op` (`include/pcbir/geometry/boolean.hpp`) wraps Clipper2's
  integer polygon-clipping engine for union/intersect/difference. Clipper2 itself is never
  named in a public PCB-IR header (header-hygiene invariant); every input/output type at this
  boundary is PCB-IR's own `Polygon`.
- **Arc flattening at this boundary is the one place in this module that is not bit-exact
  across platforms.** Clipper2 has no concept of a curved edge, so an arc span is flattened to
  a fixed number of straight chords (`ARC_FLATTEN_SEGMENTS_PER_FULL_TURN = 64` segments per
  full 360-degree turn, proportional for a partial sweep, rounded up to at least one segment)
  before crossing into Clipper2, using `std::atan2`/`std::cos`/`std::sin`/`std::hypot`.
  Placing a point on a circle at an arbitrary angle is inherently irrational in integer
  coordinates in general (unlike the cubic-Bezier midpoint bisection above, which is exact
  integer arithmetic), so this step cannot be made cross-platform bit-identical the way
  Bezier flattening is. This is a deliberate, narrower guarantee: it is documented as
  best-effort, and it is never the canonical serialized representation of the source arc --
  only a transient input to a single boolean-op call. Every polygon `boolean_op` returns is
  therefore pure-Segment, even if an input polygon had arcs.
- Fill rule: `NonZero`. Input polygons already follow this module's own winding convention
  (CounterClockwise outline, Clockwise holes -- see Geometry encoding above), which maps
  directly onto NonZero fill: a hole's opposite winding cancels the outline's contribution in
  the overlap without needing separate outline/hole tagging.
- **Deterministic input ordering**: polygons are converted to Clipper2 paths in the order
  given -- subjects then clips, each polygon's outline then its holes in declaration order,
  each contour's spans in declaration order -- and never reordered by sorting, hashing, or
  pointer identity. Clipper2's own clipping algorithm is itself deterministic (integer
  arithmetic, no unordered containers), so fixed input ordering is sufficient for the same
  logical input to always produce the same output bytes.
- Output reconstruction walks Clipper2's `PolyTree64` solution tree rather than its flat
  `Paths64` list, so nested holes and islands-within-holes are correctly reassembled into
  PCB-IR's outline-plus-holes `Polygon` shape (a flat path list alone cannot distinguish a
  hole from a disjoint second outline).

## Extensions

- Namespaced; see `docs/extensions-governance.md`.

(To be expanded with per-table layouts and golden byte examples.)
