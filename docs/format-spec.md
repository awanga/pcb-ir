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
  Hole-in-outline containment is checked: no hole chord may properly cross an outline chord
  (`pcbir::geometry::chords_properly_cross`), and a hole's first vertex must lie inside the
  outline (`pcbir::geometry::contour_strictly_contains`, an exact even-odd point-in-polygon
  test over the same chord-approximated boundary). A hole touching the outline at exactly one
  point without crossing it is not currently detected -- see the code's own documented
  boundary-case caveat.

## Diagnostic codes

`pcbir::geometry::DiagnosticCode` (`include/pcbir/geometry/diagnostics.hpp`) is the one unified,
stable code space every board-entity type's validation reports into -- a code's numeric value
never changes once shipped (new codes are only appended). `pcbir::geometry::validate(...)`
(one overload per entity type, plus a `validate(const GeometrySnapshot&)` pass that walks
every entity in every table and returns a `Diagnostic{EntityId, DiagnosticCode}` per invalid
entity) is how a caller runs this uniformly across a whole board without switching on each
entity's own per-primitive validity enum (`ContourValidity`/`PolygonValidity`/`PathValidity`).

| Code | Value | Meaning |
|---|---|---|
| `Valid` | 0 | No problem found. |
| `InvalidOutline` | 1 | An entity's outline `Contour` fails `ContourValidity` (too few spans, discontinuous, an invalid arc span, or self-intersecting). |
| `OutlineWrongOrientation` | 2 | An outline is not wound CounterClockwise. |
| `InvalidHole` | 3 | A hole `Contour` fails `ContourValidity`. |
| `HoleWrongOrientation` | 4 | A hole is not wound Clockwise. |
| `HoleOutsideOutline` | 5 | A hole crosses, or is not contained within, its outline. |
| `EmptyPath` | 6 | A `Track`/`SilkscreenGraphic`'s `Path` has no spans. |
| `DiscontinuousPath` | 7 | Consecutive spans in a `Path` don't share an endpoint. |
| `InvalidArcSpan` | 8 | An arc span (in a `Contour` or `Path`) fails `Arc::is_valid`. |
| `NonPositiveSpanWidth` | 9 | A `Path` span's `width_nm` is not positive. |
| `NonPositiveDrillDiameter` | 10 | A `Via`'s `drill_diameter_nm`, or a `DrillHit`'s `diameter_nm`, is not positive. |
| `NonPositiveFinishedHoleDiameter` | 11 | A `Via`'s `finished_hole_diameter_nm` is not positive. |
| `NonPositivePadDiameter` | 12 | A `Via`'s `pad_diameter_nm` is not positive. |
| `NonPositiveAnnularRing` | 13 | A `Via`'s derived `annular_ring_nm()` is not positive (`pad_diameter_nm` doesn't exceed `finished_hole_diameter_nm`). |

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

## Connectivity encoding

- `schemas/connectivity.fbs` (`pcbir::connectivity`, TASKS.md Phase 3) is a standalone root
  schema, the same way `schemas/geometry.fbs` is -- composed into the root snapshot schema in
  a later phase (Phase 5). It is deliberately geometry-free: no field references a shape,
  layer, or coordinate, so a net's name and membership are resolvable without loading the
  geometry layer at all.
- `Net` carries only a `name`.
- `Pin` is the connectivity layer's node: a `pad` field (the geometry-layer pad/via's stable
  `EntityId`, never the geometry itself) and a `net` field (the owning `Net`'s `EntityId`, or
  0/null for an unconnected pin). A pad is claimed by at most one `Pin` -- see Connectivity
  diagnostic codes below.
- `DifferentialPair` carries `positive_net`/`negative_net`, each a `Net` `EntityId`. Polarity
  is which field a net occupies, not a separate tag, so it cannot drift out of sync on
  round-trip.
- `Bus` carries a `name` and an ordered `members` list of `Net` `EntityId`s. Member order is
  significant (e.g. bit position in a parallel bus) and is preserved on round-trip -- FlatBuffers
  vectors are already order-preserving, so no extra encoding is needed.
- Every entry table (`NetEntry`, `PinEntry`, `DifferentialPairEntry`, `BusEntry`) carries the
  stable `EntityId` its entity was assigned in the workspace it was built from, mirroring
  `schemas/geometry.fbs`'s `PadEntry`/`ViaEntry`/etc.

## Connectivity diagnostic codes

`pcbir::connectivity::DiagnosticCode` (`include/pcbir/connectivity/diagnostics.hpp`) is its
own stable code space, scoped to the connectivity layer the same way
`pcbir::geometry::DiagnosticCode` is scoped to geometry -- unifying per-layer diagnostic
spaces into one is Post-MVP pass-manager work (TASKS.md Phase 10), not a Phase 3 requirement.
`pcbir::connectivity::validate(const ConnectivitySnapshot&)` combines each entity's own
`validate()` with the cross-entity checks (dangling net references, duplicate pad assignment,
dangling diff-pair/bus members) that need the whole net graph to decide.

| Code | Value | Meaning |
|---|---|---|
| `Valid` | 0 | No problem found. |
| `EmptyNetName` | 1 | A `Net`'s `name` is empty. |
| `InvalidPin` | 2 | A `Pin`'s `pad` is a null `EntityId`. |
| `UnconnectedPin` | 3 | A `Pin`'s `net` is a null `EntityId` (an orphan pin). |
| `DanglingPinNetReference` | 4 | A `Pin`'s `net` does not resolve to any `Net` in the snapshot. |
| `DuplicatePadAssignment` | 5 | The same pad is referenced by more than one `Pin` -- a short. |
| `DegenerateDiffPair` | 6 | A `DifferentialPair` member net is null, or both members are the same net. |
| `DanglingDiffPairMember` | 7 | A `DifferentialPair` member net does not resolve to any `Net`. |
| `EmptyBus` | 8 | A `Bus` has no member nets. |
| `DuplicateBusMember` | 9 | A `Bus` lists the same net more than once. |
| `DanglingBusMember` | 10 | A `Bus` member net does not resolve to any `Net`. |

## Stackup encoding

- `schemas/stackup.fbs` (`pcbir::stackup`, TASKS.md Phase 4) is a standalone root schema, the
  same way `schemas/geometry.fbs` and `schemas/connectivity.fbs` are -- composed into the root
  snapshot schema in a later phase (Phase 5).
- The wire format never carries floating point (see Encoding above); `Material`'s
  `dielectric_constant_e6`/`loss_tangent_e6` and `ImpedanceProfile`'s
  `target_ohm_e6`/`actual_ohm_e6` are dimensionless/electrical fixed-point fields, each the
  true value multiplied by 1e6 and stored as an `int64`. A dielectric constant (Dk) of 4.3 is
  stored as `4300000`; an impedance target of 90.0 Ω is stored as `90000000`. `Layer`'s
  `thickness_nm`/`roughness_nm` reuse the project-wide nanometer length convention instead, the
  same as every other length field in the format.
- `Material` carries a `name` plus the two fixed-point constants above. It is referenced by a
  `Layer`, never embedded, so the same material can back more than one layer.
- `Layer` is one physical layer in the board's cross-section: a `kind` (`Copper` or
  `Dielectric`), `thickness_nm`, `roughness_nm` (copper foil profile; unused for `Dielectric`),
  and a `material` reference (the owning `Material`'s `EntityId`; meaningful, and required, only
  for a `Dielectric` layer).
- `LayerStack` is the board's physical stackup: a `name` and an ordered, top-to-bottom `layers`
  list of `Layer` `EntityId`s. Order is significant (physical build-up order, and which copper
  layers are adjacent determines valid blind/buried via spans) and is preserved on round-trip,
  the same way `schemas/connectivity.fbs`'s `Bus.members` preserves its ordering.
- `ImpedanceProfile` carries controlled-impedance data -- a `class_name` (identifying a
  net-class/trace-class by name, since no such entity exists yet), a `target_ohm_e6`, and an
  `actual_ohm_e6` that may be fab-declared or solver-computed; MVP does not distinguish the
  source and carries it purely as data. Full impedance computation is Post-MVP (constraint
  system).
- A geometry `Via` (`include/pcbir/geometry/via.hpp`) already carries `start_layer`/
  `end_layer` fields referencing a stackup `Layer` by stable `EntityId`, never by geometry --
  added ahead of this phase so a through-hole via is simply the degenerate case where the span
  covers every layer, and a blind/buried/microvia is the same struct with a narrower span. No
  new via type was needed for this phase; see Stackup diagnostic codes below for how a stackup
  edit that removes a referenced layer is detected.
- Every entry table (`MaterialEntry`, `LayerEntry`, `LayerStackEntry`, `ImpedanceProfileEntry`)
  carries the stable `EntityId` its entity was assigned in the workspace it was built from,
  mirroring `schemas/geometry.fbs`/`schemas/connectivity.fbs`.

## Stackup diagnostic codes

`pcbir::stackup::DiagnosticCode` (`include/pcbir/stackup/diagnostics.hpp`) is its own stable
code space, scoped to the stackup layer the same way geometry's and connectivity's are scoped
to theirs -- unifying per-layer diagnostic spaces into one is Post-MVP pass-manager work
(TASKS.md Phase 10), not a Phase 4 requirement. `pcbir::stackup::validate(const
StackupSnapshot&)` combines each entity's own `validate()` with the cross-entity checks
(dangling layer/material references) that need the whole stackup to decide.

`pcbir::stackup::validate_via_layer_references(const GeometrySnapshot&, const
StackupSnapshot&)` is a separate, opt-in check: the one stackup diagnostic that needs a
geometry snapshot as well as a stackup one, so it is not folded into `validate()` above. It
walks every `Via` in the geometry snapshot and flags one whose `start_layer`/`end_layer` no
longer resolves to any `Layer` in the stackup snapshot -- the "a stackup edit that removes a
referenced layer is flagged, not silently corrupted" guarantee.

| Code | Value | Meaning |
|---|---|---|
| `Valid` | 0 | No problem found. |
| `EmptyMaterialName` | 1 | A `Material`'s `name` is empty. |
| `NonPositiveDielectricConstant` | 2 | A `Material`'s `dielectric_constant_e6` is not positive. |
| `NegativeLossTangent` | 3 | A `Material`'s `loss_tangent_e6` is negative. |
| `NonPositiveLayerThickness` | 4 | A `Layer`'s `thickness_nm` is not positive. |
| `NegativeLayerRoughness` | 5 | A `Layer`'s `roughness_nm` is negative. |
| `MissingDielectricMaterial` | 6 | A `Dielectric` `Layer`'s `material` is a null `EntityId`. |
| `EmptyStackup` | 7 | A `LayerStack` has no member layers. |
| `DuplicateStackupLayer` | 8 | A `LayerStack` lists the same layer more than once. |
| `EmptyImpedanceClassName` | 9 | An `ImpedanceProfile`'s `class_name` is empty. |
| `NonPositiveImpedanceTarget` | 10 | An `ImpedanceProfile`'s `target_ohm_e6` is not positive. |
| `DanglingStackupLayerReference` | 11 | A `LayerStack` member does not resolve to any `Layer` in the snapshot. |
| `DanglingLayerMaterialReference` | 12 | A `Layer`'s `material` does not resolve to any `Material` in the snapshot. |
| `DanglingViaLayerReference` | 13 | A geometry `Via`'s `start_layer`/`end_layer` does not resolve to any `Layer` in the stackup snapshot. |

## Extensions

- Namespaced; see `docs/extensions-governance.md`.

## Intent & Planning schema (reserved, Post-MVP)

`schemas/intent.fbs` (Layer 6, `TASKS.md` Phase 14) is reserved, not built, in v0.1, the same
way the archive/container layer is (`docs/architecture.md`). It will carry engineering
intent, transformation-plan/transaction history, and stable semantic-identifier alias tables
— round-tripping through the same snapshot/workspace mechanism as every other layer, so no
breaking schema change is needed to add it later. See `docs/architecture.md` → "The Intent &
Planning layer" and "Stable semantic identifiers".

(To be expanded with per-table layouts and golden byte examples.)
