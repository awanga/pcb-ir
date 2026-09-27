# RFC 0003: KiCad S-Expression Importer, Exporter, and Round-Trip Harness

- **Status:** Accepted
- **Author(s):** Claude Sonnet 5 (agentic), on behalf of Alfred Wanga
- **Date:** 2026-09-27

## Motivation

`TASKS.md` Phase 7 is the next unstarted work: the KiCad round-trip fidelity proof
(importer, exporter, round-trip harness, lossiness report). RFC 0002 built the schema
foundation this needs (`Footprint`, `BoardOutline`, `EdgeCuts` layers, `Track`/`CopperPour`
net fields) and explicitly deferred the parser/importer/exporter/harness to "a separate
RFC, built against this now-stable foundation" — this is that RFC.

RFC 0002 also recorded several open questions it could not resolve because, at the time,
"no KiCad/pcbnew installation exists in the implementation environment." That constraint no
longer holds: this device has KiCad 10.0.6 installed with a working `pcbnew` Python module
(`$KICAD_PYTHON` / `$PYTHONPATH`, recorded in agent memory). This RFC uses `pcbnew` directly
as ground truth — not the format documentation alone — to resolve those open questions with
verified, reproducible data, and proposes that the round-trip harness (Design → "Verification
against real KiCad") use `pcbnew` as an independent checker wherever it's available, going
beyond "it parses by our own reader's rules" to "it reopens in real KiCad," which is Phase
7's actual stated acceptance criterion.

This RFC covers the full follow-up scope RFC 0002 deferred: the S-expression parser, the
importer, the exporter, the round-trip fidelity harness, the lossiness report generator, and
the golden corpus plan. Per `AGENTS.md`'s change-control rule, this is elevated for approval
before any implementation begins.

## Design

### Target version pin

Per Phase 7's own "Challenges/Mitigations" ("KiCad's format drifts across releases — pin a
target KiCad version"): pin to **KiCad 10**, the version installed and verifiable in this
environment. Concretely, from a `pcbnew`-generated reference file:

```
(kicad_pcb
  (version 20260206)
  (generator "pcbnew")
  (generator_version "10.0")
  ...
```

The importer accepts `version >= 20260206` (KiCad's own board-format-version field, not to
be confused with PCB-IR's own `FormatVersion`) and rejects earlier or unrecognized values
with a documented diagnostic rather than attempting best-effort parsing — consistent with
`docs/format-spec.md`'s existing "reject unknown major" posture for PCB-IR's own wire format.
Older KiCad versions (6/7/8/9) are out of scope for this RFC; broadening version support is
future work once a corpus of real files from those versions is available to verify against.

### Module layout

Following the existing flat-by-concern layout (`docs/architecture.md` → "Repository layout",
"Current (v0.1 MVP)"), parallel to `include/pcbir/{core,geometry,connectivity,stackup}` /
`src/pcbir/{core,geometry,connectivity,stackup}`:

```
include/pcbir/kicad/
    sexpr.hpp        -- generic S-expression node type + parser (KiCad-agnostic)
    import.hpp        -- import_kicad_pcb(path) -> BoardSnapshot
    export.hpp        -- export_kicad_pcb(BoardSnapshot, path)
    fidelity.hpp      -- FidelityReport, FidelityTier (Preserved/Approximated/Lost/Unsupported)
src/pcbir/kicad/
    sexpr.cpp
    import.cpp
    export.cpp
    fidelity.cpp
tests/kicad/
    sexpr_test.cpp
    import_test.cpp
    export_test.cpp
    roundtrip_test.cpp
tests/corpus/kicad/
    <board-name>/board.kicad_pcb
    <board-name>/expected.json          -- hand-verified semantic summary
```

No new schema, no new C ABI surface, and no new third-party dependency (the S-expression
grammar is simple enough — parenthesized lists of atoms/strings/numbers — that a hand-rolled
recursive-descent parser is a few hundred lines, consistent with the "lean, tiered
dependencies" / "add a dependency only for significant perf gain, code-size reduction, or
complexity reduction" policy).

### S-expression parser (`sexpr.hpp`)

A minimal, KiCad-agnostic tokenizer/parser producing a generic tree:

```cpp
struct SExpr {
  enum class Kind { List, Symbol, String, Number };
  Kind kind;
  std::string text;            // Symbol/String/Number, verbatim
  std::vector<SExpr> children; // List only
};
SExpr parse_sexpr(std::string_view text);
```

This mirrors import/export as a compilation pipeline (`docs/architecture.md` →
"Import/export as compilation"): `sexpr.hpp` is the *lexer/parser* stage (syntax only, no
KiCad semantics), and `import.cpp` is the *semantic analysis* stage that walks the generic
tree and knows what `(footprint ...)` or `(via ...)` mean. Keeping these separate means the
parser is trivially unit-testable against made-up S-expressions without needing real KiCad
fixtures, and a future format sharing S-expression syntax (Eagle's newer format also uses
S-expressions in places) could reuse the parser stage.

### Net identity: no on-disk net table (verified, differs from older KiCad recollection)

Building and round-tripping a probe board through `pcbnew` (script → `SaveBoard` →
`LoadBoard` → `SaveBoard` again, byte-identical on the second save) established that KiCad
10's board format has **no top-level net-declaration table and no net-code integers at usage
sites** — every copper item just carries `(net "NAME")`, a bare name string:

```
(pad "1" smd rect (at -1 0) (size 1.6 1.2) (layers "F.Cu" "F.Mask" "F.Paste") (net "SIG1") ...)
(segment (start 15 10) (end 20 15) (width 0.25) (layer "F.Cu") (net "SIG1") ...)
```

confirmed against `board.GetNetInfo()` internally still assigning numeric codes (0 = no-net,
1, 2, ...) purely as a runtime concept `pcbnew` derives on load — not a file-format concept.
This is a real, previously-unverified fact (superseding an earlier, incorrect assumption
that a `(net <code> "<name>")` table exists on disk, which held in some older KiCad
versions/other CAD tools but not here) and it simplifies the importer: net identity is the
name string alone. The importer's first pass over the file collects the distinct set of
`(net "NAME")` strings actually referenced by pads/vias (via `connectivity::Pin`)/
tracks/pours, and creates one `connectivity::Net` per distinct name; the exporter needs no
net-numbering logic at all, only the name.

### Footprint placement: the mirror/rotate composition rule (resolves RFC 0002's open question)

RFC 0002 flagged that "KiCad's back-side (`B.Cu`) footprint mirror/rotate composition order
could not be resolved from the official S-expression format documentation alone ... needs
either real sample files or reading `pcbnew` source directly." Building probe boards and
inspecting `pcbnew`'s own internal state before/after `FOOTPRINT::SetLayerAndFlip()`
resolved this exactly. Given a footprint at absolute position `P`, orientation `φ` (degrees),
and a pad within it at footprint-local (unrotated) offset `(x, y)` with its own local delta
angle `δ` relative to the footprint (so the pad's true absolute rotation is `φ + δ`),
flipping the footprint to the opposite side transforms:

```
new_φ  = (180 - φ)  mod 360        // footprint orientation field
new_δ  = (180 - δ)  mod 360        // per pad, each pad's stored local delta angle
new_(x, y) = (x, -y)               // per pad, negate the LOCAL Y offset only
```

with the footprint's absolute position `P` itself **unchanged**, and each pad's absolute
position recomputed the normal way (`geometry::rotate((x, y), φ, origin)`) using the *new*
`φ`/`(x, y)` — no separate mirror step is needed once these three substitutions are made
before the existing rotate math runs, since the composed result already equals negating the
pad's original absolute rotation (`φ_new + δ_new = 180 - φ + 180 - δ = 360 - (φ + δ) ≡
-(φ + δ)`, verified numerically against two independent test cases including a non-90°
orientation). Layer names swap front↔back (`F.Cu`↔`B.Cu`, `F.Mask`↔`B.Mask`,
`F.Paste`↔`B.Paste`, `F.SilkS`↔`B.SilkS`, `F.Fab`↔`B.Fab`, `F.CrtYd`↔`B.CrtYd`,
`F.Adhes`↔`B.Adhes`), and text properties (reference/value) gain `(justify mirror)` in
their `effects` block, a rendering-only annotation the exporter must reproduce for visual
fidelity but which carries no positional meaning.

The importer uses this rule in reverse (KiCad file → PCB-IR): read `φ`, `(x, y)`, `δ` as
stored, compute each pad's final **absolute** position/rotation via `geometry::rotate`
directly (the schema stores absolute pad geometry, not footprint-relative — see below), and
sets `Footprint::side = Bottom` when the footprint's own `(layer "B.Cu")` says so, without
needing to un-mirror anything, since PCB-IR's `Footprint` records `position`/`rotation_e6`/
`side` as read, and `Pad`/`Via` already hold final resolved geometry. The **exporter** is
where this rule is load-bearing in the other direction: given a `Pad`'s absolute `position`/
`outline` and its owning `Footprint`'s `position`/`rotation_e6`/`side`, it must invert
`geometry::rotate` to recover the footprint-local `(x, y, angle)` triple to emit, applying
the flip transform above when `side == Bottom` — this is exact for 90°-multiple angles (the
common case) via the same integer swap/negate `geometry::rotate` already uses, and a
documented best-effort floating-point inverse otherwise, mirroring `rotate.hpp`'s existing
precision contract.

### Pad shape fidelity: a real, previously-unflagged gap

`geometry::Pad` (`include/pcbir/geometry/pad.hpp`) stores only `position` and an already-
resolved `outline: Polygon` — there is no shape-parameter field (no rect/circle/oval/
roundrect/trapezoid/custom distinction, no width/height/corner-radius). This is a genuine
fidelity gap the schema-foundation RFC didn't need to address (it wasn't populating pads
from real files yet) but the importer must now confront directly: KiCad pads are
authored parametrically (`(pad "1" smd rect (size 1.6 1.2) ...)`,
`(pad "2" smd circle (size 1.0 1.0) ...)`, `roundrect` with a corner-radius ratio, etc.), and
naively polygonizing every pad shape on import would be a silent, permanent loss of the
"this is a rect/circle/oval pad" authored fact — re-exporting would then only ever be able to
emit a generic `custom` pad shape from the polygon, degrading every non-rectangular pad from
**Preserved** to **Approximated** on the very first round-trip, and producing files that,
while geometrically correct, look nothing like what a KiCad user authored (worse tool
ergonomics, and a real regression risk for downstream Gerber/DRC tools that special-case
primitive pad shapes).

**Proposed resolution, requiring no schema change:** use the extension mechanism already
built for exactly this purpose (Phase 5, `schemas/snapshot.fbs` → `ExtensionEntry`, domain
`Geometry`, keyed by the `Pad`/`Via`'s `EntityId`). A new registered extension,
`ext_namespace = "PCBIR_KICAD"`, `name = "pad_shape"`, `version = 1`, with a small typed
payload (shape kind enum, width/height, corner-radius ratio where applicable, local
orientation) carries the original parametric shape losslessly alongside the geometric
`Polygon` outline every reader already gets regardless. A reader that understands the
extension reconstructs the original KiCad pad primitive exactly (**Preserved**); a reader
that doesn't still gets fully correct copper geometry from `outline` alone (graceful
degradation — the extension mechanism's documented contract), just without the "this was
authored as a rect" fact, which is the intended, and only, degradation path. This needs no
new RFC of its own since it's additive use of an already-accepted, already-generic
mechanism — flagged here rather than silently decided because it's a real design choice with
a fidelity-classification consequence (whether pad shape is Preserved or merely
Approximated), which `AGENTS.md`'s change-control spirit says should be visible before
building on top of it, even though it doesn't cross the schema/ABI-change approval line
itself.

### UUID/`tstamp` generation (resolves RFC 0002's open question)

KiCad's exported items each carry a `(uuid "...")` (standard UUID-v4-shaped string, though
KiCad itself doesn't enforce version/variant bits meaningfully). RFC 0002 required this be "a
pure, specified function of `(domain, entity kind, EntityId.value())` — never random, never
pointer/address-based hashing" to preserve PCB-IR's determinism invariant. Proposed scheme:
**UUID v5** (name-based, SHA-1), using a fixed PCB-IR namespace UUID constant (generated once,
committed in `docs/format-spec.md`, never changed) and a name string of the form
`"pcbir:<domain>:<entity_kind>:<entity_id>"` (e.g. `"pcbir:geometry:pad:42"`). UUIDv5 is a
standard, well-specified algorithm (RFC 4122) requiring only SHA-1, which is small enough to
implement directly (~100 lines, no new dependency, consistent with the dependency policy) or
already available via the platform's existing crypto surface if one is already linked
(neither is currently the case, so a small internal implementation is proposed). This makes
every exported UUID a pure, reproducible function of the entity's identity, satisfying the
determinism invariant, and stable across re-exports of the same snapshot (a real ergonomic
win for diffing exported files across PCB-IR versions).

### Scope: first-pass entity coverage

Mirroring RFC 0002's own deliberate scoping, the first importer/exporter pass covers exactly
the entities the schema foundation added support for, and no more:

**In scope:** board outline (`Edge.Cuts` → `BoardOutline`), copper layers + stackup
(`(layers ...)` → `stackup::Layer`/`LayerStack`), footprints with SMD pads and though-hole
pads-as-`Via` (`(footprint ...)`/`(pad ...)` → `Footprint`/`Pad`/`Via`), tracks and arcs
(`(segment ...)`/`(arc ...)` → `Track`), free-standing vias (`(via ...)` → `Via`), copper
zones/pours (`(zone ...)` → `CopperPour`), and nets (as established above).

**Explicitly out of scope for this pass** (reported as **Unsupported**, not silently
dropped): rule areas/keepout zones beyond the existing `Keepout` shape, dimension objects,
3D model references, group objects, design rules (`(setup ... constraints)`), net classes,
and any KiCad-specific pad shape this RFC's extension mechanism doesn't yet enumerate
(e.g. custom-shape pads with primitive lists). **Also moved out of scope during
implementation** (correcting this RFC's original draft, which listed it as in-scope):
silkscreen graphics (`(gr_line ...)`/`(fp_text ...)` on `F.SilkS`/`B.SilkS` → geometry's
existing `SilkscreenGraphic`). Implementing this exposed a real gap `stackup::LayerKind`
doesn't cover — `{Copper, Dielectric, EdgeCuts}` has no non-physical layer identity for
`F.SilkS`/`B.SilkS` the way `EdgeCuts` covers `Edge.Cuts` — and adding one, while small and
directly precedented by RFC 0002's own `EdgeCuts` addition, is still a schema change
requiring its own elevated approval rather than being decided silently mid-implementation.
It's deferred to a small, narrowly-scoped follow-up RFC once the schema-change-free core
above lands. Each of these is a candidate for a later, additive pass — none requires a
breaking schema change, since `Keepout`/`SilkscreenGraphic` already exist and net
classes/design rules are Phase 12's eventual concern.

### Round-trip fidelity harness

Two independent checks, not one, since Phase 7's AC has two parts ("re-opens in KiCad" and
"semantically equal... for every corpus board"):

1. **Self-contained semantic-equality check** (required, runs in CI, no KiCad needed):
   `import → IR snapshot A → export → import again → IR snapshot B`, then structurally
   compare `A` and `B` (nets, layers, geometry, footprint membership) for equality up to the
   documented Approximated-tier tolerances (e.g. the existing Bezier-flattening tolerance,
   non-90°-rotation floating-point tolerance). This is the harness's core and is exactly the
   pattern the geometry/serialization determinism tests already use — no new infrastructure
   category, reuse of the existing snapshot-comparison approach.
2. **Real-KiCad verification** (optional, environment-gated, not a CI requirement): where
   `$KICAD_PYTHON`/`pcbnew` are available (recorded per-device, not assumed), additionally
   `pcbnew.LoadBoard()` the exported file and confirm it loads without error/exception —
   the literal, direct test of Phase 7's "exported board re-opens in KiCad" AC, which no
   amount of spec-reading can substitute for. This check is skipped gracefully (reported, not
   silently ignored) when no KiCad installation is detected, so CI (which has no KiCad
   installed on any of the three platforms per Phase 0) isn't blocked on it; it runs locally
   in this development environment and is recommended as a pre-merge manual check for Phase 7
   commits here, with installing KiCad in CI left as a documented open question below rather
   than assumed.

### Lossiness report generator

A `FidelityReport` (`fidelity.hpp`) produced by both import and export: a list of
`(entity, attribute, tier, reason)` records using the four tiers already defined in
`docs/conformance.md` (Preserved/Approximated/Lost/Unsupported). Every entity the importer
touches gets at least one record; a **Preserved**-only report is the target for the
in-scope entity set on the golden corpus. This directly implements the "Lossiness report
generator" task and the tier table `docs/conformance.md` already stubs out — no new
classification scheme invented.

### Golden corpus

`tests/corpus/kicad/<board-name>/board.kicad_pcb` — generated via `pcbnew` scripting (as
this RFC's probe boards were) so every corpus board is itself guaranteed to be valid,
real-KiCad-authored input, not hand-typed S-expressions that might not match what KiCad
actually emits. Starts with 3-4 boards of increasing complexity (single footprint + track;
multi-layer with vias and a zone; back-side footprints exercising the mirror rule; a board
exercising every in-scope entity type at once), each with a hand-verified
`expected.json` semantic summary (net list, layer list, entity counts) the harness checks
imported snapshots against independent of the round-trip-equality check.

## Alternatives considered

- **Depend on a third-party S-expression or KiCad-parsing library** — rejected: the grammar
  is simple enough that a hand-rolled parser is small and fully under PCB-IR's own
  determinism/testing discipline, and no existing library matches PCB-IR's "no new
  dependency without significant benefit" bar for something this size.
- **Add explicit pad-shape fields to `geometry::Pad` instead of using the extension
  mechanism** — rejected for this RFC: that would be an actual schema change requiring its
  own elevated approval (a new gate this RFC doesn't need to cross), whereas the extension
  table already exists, is already schema, and was purpose-built for exactly this case
  (namespaced, versioned, safely-ignorable per-entity data). Revisit only if a future need
  demonstrates the extension path is insufficient (e.g. needing pad shape to participate in
  core geometry validation, which it doesn't today).
- **Derive net identity from a hoped-for on-disk net-code table** — rejected once probe
  boards showed KiCad 10 doesn't write one; building the importer around a table that
  doesn't exist would have been a defect discovered only against real files, which is
  exactly why this RFC insists on `pcbnew`-verified ground truth rather than
  documentation/memory alone.
- **Require KiCad installed in CI for the harness's primary check** — rejected: CI's three
  platforms (Phase 0) have no KiCad installed today, and adding it is a nontrivial CI change
  in its own right; the self-contained semantic-equality check is a strictly necessary and
  sufficient CI gate on its own, with real-KiCad verification as a valuable but
  environment-gated supplement, not a hard CI dependency this RFC should introduce silently.

## Backward compatibility

- **Schema:** no change. Pad-shape fidelity uses the existing `ExtensionEntry` mechanism
  (Phase 5); no new table/field is added to `schemas/*.fbs`.
- **C ABI:** no change. Import/export are new library entry points
  (`pcbir::kicad::import_kicad_pcb`/`export_kicad_pcb`), not yet exposed through
  `include/pcbir/pcbir.h`; C-ABI exposure (if wanted) is a separate, later, additive change.
- **MCP service contracts:** unaffected (not yet built).

## Translation-fidelity impact

This RFC is the first to actually populate the preserved/approximated/lost/unsupported
classification for a real format pair (`docs/conformance.md`). Summary for the in-scope
entity set: geometry, stackup/layers, footprints (including back-side placement), tracks,
vias, zones, and nets are **Preserved**; pad shape is **Preserved** when the
`PCBIR_KICAD/pad_shape` extension round-trips through a reader that understands it and
**Approximated** (geometry only, shape identity lost) through one that doesn't; non-90°
footprint/pad rotations are **Approximated** at the same documented floating-point tolerance
`geometry::rotate` already carries; everything listed under "Explicitly out of scope for this
pass" above is **Unsupported**, never silently dropped.

## Open questions

- Exact S-expression parameterization of `(arc ...)` track/graphic segments (KiCad encodes
  these as three points — start/mid/end — not PCB-IR's endpoints+center/sweep form) needs a
  verified, deterministic conversion; this is a smaller instance of the same
  approximation-boundary discipline as Bezier flattening but hasn't been probed against real
  `pcbnew` output yet and should be nailed down during implementation, verified against the
  golden corpus, before being written into `docs/format-spec.md`.
- Zone (copper pour) fill-polygon encoding (KiCad stores both the authored outline and,
  optionally, computed fill polygons) — this RFC's scope is the authored outline only
  (`CopperPour.outline`); whether/how to carry computed fill data is deferred, likely via the
  same extension mechanism as pad shape if ever needed.
- Whether to add KiCad to the CI images so the real-KiCad-verification check can eventually
  run in CI too, not just locally on this device, is left open — a CI infrastructure decision
  independent of this RFC's design, not blocking it.
- Custom-shape pads (primitive-list-defined, not parametric rect/circle/oval/roundrect) are
  Unsupported in this pass; whether they eventually get their own extension payload or a
  polygon-only Approximated treatment is deferred until a real corpus board needs one.
