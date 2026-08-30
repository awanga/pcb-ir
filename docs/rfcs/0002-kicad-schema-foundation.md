# RFC 0002: KiCad Schema Foundation

- **Status:** Accepted
- **Author(s):** Claude Sonnet 5 (agentic), on behalf of Alfred Wanga
- **Date:** 2026-08-29

## Motivation

The roadmap's next item is the KiCad round-trip fidelity proof: an S-expression importer,
exporter, round-trip harness, and lossiness report. Before any parser code can be written,
research (three read-only Explore passes, a design pressure-test pass, and the official KiCad
developer documentation) established that the roadmap's own acceptance criterion for this work
-- "board → IR: geometry, **footprints**, nets, stackup" and "semantically equal in **nets**,
layers, geometry" -- requires schema-level additions that don't exist yet:

1. **No `Footprint`/component entity anywhere.** Pads and vias have no grouping concept at all;
   nothing in the model corresponds to a placed part.
2. **No board-outline entity.** Nothing represents KiCad's `Edge.Cuts` layer; an "imported
   board" would otherwise have no shape.
3. **No non-physical layer identity.** `stackup::LayerKind` is `{Copper, Dielectric}` only, but
   every geometry entity's `LayerRef` resolves into that same one `Layer` table -- confirmed
   directly against KiCad's own file format: its `(layers ...)` section is exactly this kind of
   unified physical-plus-technical layer list, which is direct precedent for extending the
   existing table rather than inventing a second, parallel one.
4. **`Track`/`CopperPour` have no net field.** KiCad's `segment`/`arc`/`zone` elements always
   carry an explicit net; deriving one geometrically would require a full copper-connectivity
   flood-fill engine, which is out of scope (roadmap "Routing graph extraction," a later,
   larger, and separately-planned body of work). Without a field, re-exported copper would
   silently lose net identity, which breaks the acceptance criterion outright.

Per `AGENTS.md`'s change-control rule, any change to the serialized schema requires explicit,
written-justification approval before implementation -- the same gate the stable C ABI went
through (`docs/rfcs/0001-stable-c-abi.md`). This RFC is that written justification, and its
acceptance is the required sign-off.

This RFC covers only the schema foundation (new entities/fields, their diagnostics, the
FlatBuffers schema, serialization, and full unit test coverage) -- fully unit-testable today,
with zero new dependencies, entirely inside the existing `pcbir_core` target. The
S-expression parser, importer, exporter, fidelity harness, lossiness-report type, and golden
corpus are a separate, materially larger and harder-to-verify body of work (no KiCad/pcbnew
installation exists in the implementation environment, so "exported board re-opens in KiCad"
cannot be end-to-end verified there regardless) and are deliberately left to a future RFC, built
against this now-stable foundation rather than co-evolved with it.

## Design

### Stackup: one new `LayerKind` value, one bug fix

- `stackup::LayerKind` gains `EdgeCuts = 2` (append-only). This is a real, if small, semantic
  broadening of `Layer`: it previously documented itself as "one physical layer in the board's
  cross-section," which is no longer accurate now that a non-physical, mechanical/drafting
  layer identity shares the same table. The type now documents itself as "one layer in the
  board's layer-identity space; only `Copper`/`Dielectric` participate in the physical
  stackup."
- `validate(const Layer&)`'s `thickness_nm <= 0` check was unconditional -- it is now
  conditioned on `kind != EdgeCuts` (mirroring how the `material`-required check is already
  conditioned on `kind == Dielectric`). An `EdgeCuts` layer's `thickness_nm == 0` is its normal,
  expected value (it has no z-height), not a validation failure; without this fix, every
  imported board outline's layer would fail validation immediately.
- New diagnostic `NonPhysicalStackupLayerMember` (14): an `EdgeCuts` layer must never appear
  inside a `LayerStack`, which models physical build-up order.
- `validate_via_layer_references` is renamed `validate_layer_references` and extended to also
  check a new `BoardOutline.layer` resolves to a `Layer{kind = EdgeCuts}` specifically (new
  codes `DanglingBoardOutlineLayerReference` (15), `BoardOutlineLayerWrongKind` (16) --
  distinguished because the two call for different fixes). This function is not exposed
  through the C ABI (`pcbir.h` covers only `{Via, Net, Pin, Material, Layer}` today), so the
  rename is a pure internal change with no ABI impact.

### Geometry: two new entities, two new fields on existing entities, one new primitive

- `Pad` and `Via` gain `std::string pad_number` (the footprint-local pad identifier, e.g. `"1"`
  or `"A1"`). Without it, a `Footprint.pads` list's order would be the only way to recover which
  pad is which -- correct for sequential numbering, silently wrong for any alphanumeric/BGA
  pad naming. Empty is legitimate (KiCad allows blank numbers for mechanical/fiducial pads).
- New `geometry::Footprint`: `reference_designator`, `value`, `position`, `rotation_e6` (degrees
  * 1e6 -- extends the project's existing `_e6` fixed-point convention, previously used only for
  dimensionless physical/electrical constants, to angular measure for the first time), `side`
  (`FootprintSide::Top`/`Bottom`), and an ordered `pads: vector<EntityId>` mixing `Pad` and
  `Via` ids -- well-defined because a `Workspace` allocates one shared `EntityId` counter across
  every component table. Mirrors `connectivity::Bus`'s proven ordered-`EntityId`-list shape;
  unlike `Bus.members`, member order is preserved on round-trip but not itself semantically
  load-bearing now that each member's own `pad_number` carries identity.
- New `geometry::BoardOutline`: `outline: Polygon`, `layer: LayerRef` (must resolve to a
  `Layer{kind = EdgeCuts}`). Structurally identical to `CopperPour`/`Keepout`/`MaskOpening`, a
  separate component table purely for semantic distinction -- directly precedented by the
  existing `Track`/`SilkscreenGraphic` pair (identical shape, distinct tables). Zero-to-many
  per board: a panelized/breakaway board has genuinely disjoint outline fragments.
- `Track` and `CopperPour` gain `core::EntityId net = {}` (null = unassigned, mirroring
  `connectivity::Pin.net`'s existing convention). Both types' doc comments previously stated
  "net membership ... is not modeled here"; that sentence is now false and has been replaced
  with: an optionally recorded, *authored/claimed* net -- not derived or verified against
  actual geometric connectivity (deriving it is out of scope for this RFC) -- matching the same
  trust boundary `connectivity::Pin.net` already has, just recorded on the copper side too. No
  new header dependency (`core::EntityId` is already used for `LayerRef`); the connectivity
  layer's existing "nets resolvable without consulting geometry" property is unaffected, since
  that is about `ConnectivitySnapshot` staying self-sufficient, not about geometry never
  pointing outward.
- New `geometry::rotate` (`Point`/`Segment`/`Arc`/`Span`/`Contour`/`Polygon` overloads): rotates
  counterclockwise by an angle (degrees * 1e6) about an origin. General infrastructure, not
  KiCad-specific -- any importer placing footprint-relative geometry at absolute board
  coordinates needs it, and nothing in the codebase provided it before this RFC. Exact (bit-
  identical across platforms) for angles that are a multiple of 90 degrees, via integer
  swap/negate -- the overwhelming majority of real footprints. For any other angle, falls back
  to double-precision `cos`/`sin` rounded half-away-from-zero (`std::llround`), the same
  documented best-effort-approximation approach already accepted at the Clipper2
  arc-flattening boundary (`boolean_op`); not guaranteed bit-exact the way the 90-degree path
  is, and never the canonical representation of a rotation performed exactly at the point of
  authoring (only a computed placement).
- `GeometrySnapshot`/`GeometryWorkspace` extend from eight to ten component types
  (`Pad, Via, Track, CopperPour, Keepout, DrillHit, MaskOpening, SilkscreenGraphic, Footprint,
  BoardOutline`).
- `validate(const GeometrySnapshot&)` previously did per-entity dispatch only. It now also does
  cross-entity Footprint-membership checks (new codes `EmptyReferenceDesignator` (14),
  `DanglingFootprintMemberReference` (15), `DuplicateFootprintMemberReference` (16) -- a
  dangling check resolves against the *union* of the `Pad` and `Via` tables, not just one),
  mirroring `connectivity::validate(const ConnectivitySnapshot&)`'s existing
  per-entity-plus-cross-entity structure rather than inventing a new one.
- `connectivity::diagnostics.hpp` gains its first-ever include of a geometry header
  (`pcbir/geometry/serialize.hpp`), mirroring the existing one-directional stackup-depends-on-
  geometry precedent (`validate_layer_references`). New function
  `validate_geometry_net_references(GeometrySnapshot, ConnectivitySnapshot)` and diagnostic
  `DanglingGeometryNetReference` (11) check `Track.net`/`CopperPour.net` against the `Net`
  table -- a separate, opt-in check for the same reason `validate_layer_references` is: a
  caller with only a `ConnectivitySnapshot` never needs to pay for it.

### Schema (`schemas/geometry.fbs`, `schemas/stackup.fbs`)

All changes are additive/append-only: new fields are appended to the end of existing tables,
new tables (`Footprint`, `FootprintEntry`, `BoardOutline`, `BoardOutlineEntry`) and the new
`FootprintSide`/extended `LayerKind` enums are new declarations, and `GeometrySnapshot` gains
two new optional vector fields. No existing field changes type, position relative to other
already-shipped fields, or meaning.

## Alternatives considered

- **A separate, non-physical "graphical layer" concept, distinct from `stackup::Layer`, with
  its own reference type** -- rejected in favor of widening the existing `Layer`/`LayerKind`.
  `LayerRef` is already the one layer-identity space every geometry entity resolves into;
  introducing a second one would force every existing layer-scoped entity to gain a
  "which space" discriminator, a worse asymmetry than broadening one enum's documented scope.
  KiCad's own unified `(layers ...)` list is direct precedent that this unification is the
  natural one, not merely the cheaper one.
- **Recovering pad identity purely from `Footprint.pads`' vector order** -- rejected: KiCad pad
  numbering is not generally sequential (BGA/grid-array pads are `"A1"`, `"B12"`, not integers
  in authoring order), so this would silently break for the first non-sequentially-numbered
  footprint with no diagnostic to catch it. A small additive `pad_number` field on `Pad`/`Via`
  is cheap and precedented (the same shape of change as adding `net` to `Track`/`CopperPour`).
- **`rotation_e3` (millidegrees) instead of `rotation_e6`** -- rejected: no existing `_e3`
  convention exists anywhere in the schema (only `_e6`, used for `Material`/`ImpedanceProfile`'s
  dimensionless constants), and KiCad rotations in practice carry more than three decimal
  digits of precision; choosing too little precision is irreversible information loss on first
  import, while the storage cost of `_e6` vs. `_e3` is identical (both `int64`).
- **Deriving `Track`/`CopperPour` net membership geometrically instead of storing it** --
  rejected as infeasible for this RFC's scope: it would require a full copper-connectivity
  flood-fill/routing-graph-extraction engine, a materially larger and separately-planned body
  of work. Storing an authored/claimed net (unverified, like `Pin.net` already is) is the
  minimal change that preserves KiCad's own authored data losslessly.
- **Folding the new Footprint/BoardOutline cross-entity checks into per-entity `validate()`
  instead of the whole-snapshot pass** -- rejected: a dangling/duplicate member check is
  inherently cross-entity (only decidable by scanning every `Footprint` and both the `Pad` and
  `Via` tables together), exactly like connectivity's existing `DuplicatePadAssignment`/
  `DanglingPinNetReference` checks, whose structure this mirrors directly rather than
  reinventing.
- **A single combined KiCad-support RFC covering schema and importer/exporter/harness/corpus
  together** -- rejected: the schema foundation is fully unit-testable today with zero new
  dependencies, while the parser/importer/exporter is a materially larger, harder-to-verify
  unit of work with no KiCad installation available to check against in the implementation
  environment. Landing the foundation first, fully reviewed and tested on its own, gives the
  later importer/exporter RFC a stable, already-correct target instead of co-evolving schema
  and parser design in one pass.

## Backward compatibility

- **Schema:** additive only. Every new field is appended to the end of an existing table;
  every new table and enum value is a new declaration. No existing reader that already handles
  unknown-field/unknown-enum-value tolerance (per `docs/format-spec.md`'s versioning policy)
  needs any change to keep working; a board written before this RFC still deserializes
  correctly (`Footprint`/`BoardOutline` vectors default to empty, `pad_number` defaults to
  empty, `net` defaults to null).
- **C ABI:** unaffected. `validate_via_layer_references`'s rename is a pure internal C++ change;
  it is not, and has never been, exposed through `pcbir.h`.
- **MCP service contracts:** unaffected (not yet built).

## Translation-fidelity impact

This RFC adds the schema surface a KiCad importer/exporter will need for the "preserved" tier of
KiCad's `Footprint`, board outline, and copper-net-assignment semantics. It does not itself
implement any import/export logic, so no format's actual preserved/approximated/lost/unsupported
classification changes yet -- that classification is scoped in the follow-up importer/exporter
RFC, which this foundation exists to support.

## Open questions

- The full S-expression parser, importer, exporter, round-trip fidelity harness,
  lossiness-report type (`docs/conformance.md`'s currently-stub golden-file-corpus section), and
  hand-authored golden corpus are deliberately out of scope here and will need their own RFC.
- Whether a `Keepout`'s `LayerRef` should eventually be checked against a specific `LayerKind`
  (today, no geometry entity's `LayerRef` is checked for kind-compatibility except the new
  `BoardOutline`/`EdgeCuts` pair added by this RFC) is an open, pre-existing gap this RFC does
  not attempt to close in general.
- Whether `Silkscreen`/`SolderMask` deserve their own `LayerKind` values (needed before
  `SilkscreenGraphic`/`MaskOpening` could be meaningfully KiCad-imported) is deferred to the
  importer RFC, which will decide the first pass's exact entity coverage.
- KiCad's back-side (`B.Cu`) footprint mirror/rotate composition order could not be resolved
  from the official S-expression format documentation alone (checked both the board-format and
  common-definitions pages) and needs either real sample files or reading `pcbnew` source
  directly -- flagged for whoever implements the importer, not resolved by this RFC.
- UUID/`tstamp` generation for KiCad-format export must be a pure, specified function of
  `(domain, entity kind, EntityId.value())` -- never random, never pointer/address-based
  hashing -- so that PCB-IR's own determinism invariant holds; the exact algorithm is left to
  the importer/exporter RFC.
