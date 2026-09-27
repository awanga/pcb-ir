# RFC 0004: Add `LayerKind::Silkscreen`

- **Status:** Accepted
- **Author(s):** Claude Sonnet 5 (agentic), on behalf of Alfred Wanga
- **Date:** 2026-09-27

## Motivation

Implementing the KiCad importer (RFC 0003) against its own declared in-scope entity set
surfaced a real gap: `SilkscreenGraphic` (`include/pcbir/geometry/silkscreen_graphic.hpp`,
already part of the v0.1 schema) carries a `LayerRef` that must resolve to a stackup `Layer`,
but `stackup::LayerKind` is `{Copper, Dielectric, EdgeCuts}` -- there is no non-physical layer
identity for `F.SilkS`/`B.SilkS`, the way `EdgeCuts` (RFC 0002) covers `Edge.Cuts`. RFC 0002's
own "Open questions" flagged this exact gap and deferred the decision to the importer RFC;
RFC 0003 in turn deferred it further rather than decide it silently mid-implementation. This
RFC is that decision, made explicit and elevated before implementation, per `AGENTS.md`.

## Design

Add exactly one new value to `stackup::LayerKind` (`schemas/stackup.fbs` and
`include/pcbir/stackup/layer.hpp`), appended (never inserted) so existing values keep their
numeric identity:

```cpp
enum class LayerKind : uint8_t { Copper = 0, Dielectric = 1, EdgeCuts = 2, Silkscreen = 3 };
```

`Silkscreen` is non-physical (no Z-height, no material), exactly like `EdgeCuts`: two Layer
entities of kind `Silkscreen` exist per board (one for `F.SilkS`, one for `B.SilkS`), each
just a name -- side is not itself encoded in `LayerKind` any more than front/back copper is
(that distinction already lives in the `Layer.name` string and, for copper, stackup order).

Consequential changes, mirroring exactly how RFC 0002 threaded `EdgeCuts` through the
codebase:

- `validate(const Layer&)`'s `thickness_nm <= 0` check, currently conditioned on
  `kind != EdgeCuts`, becomes conditioned on `kind != EdgeCuts && kind != Silkscreen` (a
  Silkscreen layer's `thickness_nm == 0` is its normal, expected value).
- `NonPhysicalStackupLayerMember`'s check (a `LayerStack` member must not be a non-physical
  layer) extends to reject `Silkscreen` the same way it already rejects `EdgeCuts`.
- The C ABI's `pcbir_layer_kind_t` (`include/pcbir/pcbir.h`) gains a matching
  `PCBIR_LAYER_KIND_SILKSCREEN`, additive, mirroring `PCBIR_LAYER_KIND_EDGE_CUTS`.
- FlatBuffers round-trip mapping (`src/pcbir/stackup/serialize.cpp`) gains the new value;
  RFC 0002 already fixed the two-way-ternary bug that would have silently mis-mapped any
  third value to `Dielectric`, so this is a pure additive case, not a repeat of that bug.

This unblocks the deferred half of RFC 0003's in-scope entity list: `SilkscreenGraphic`
import (`(gr_line ...)`/similar on `F.SilkS`/`B.SilkS`) can now resolve a real `LayerRef`.

## Alternatives considered

- **A generic `Technical` catch-all `LayerKind`** covering silkscreen, mask, paste,
  courtyard, fabrication, etc. in one value -- rejected for now: nothing in the currently
  in-scope entity set needs mask/paste/courtyard/fabrication layer identity yet (only
  `SilkscreenGraphic` does), and collapsing distinct KiCad layer types into one opaque
  `Technical` kind would make a future consumer unable to tell "this is silkscreen" from
  "this is a courtyard outline" without inspecting the layer name string, which defeats the
  point of a typed `LayerKind`. Narrow and precedented (one value per real, in-scope need,
  exactly as `EdgeCuts` was added for `BoardOutline` and nothing else) beats speculative
  breadth.
- **Encode front/back as part of `LayerKind`** (e.g. `SilkscreenFront`/`SilkscreenBack`) --
  rejected: `Copper` doesn't do this for `F.Cu`/`B.Cu` either; side is already carried by
  `Layer.name` and, where it matters, stackup order. Adding a front/back split only for
  `Silkscreen` would be an inconsistent, one-off exception to that existing convention.

## Backward compatibility

- **Schema:** additive only -- a new enum value appended after `EdgeCuts`. A pre-existing
  file never contains a `Silkscreen`-kind `Layer` (nothing wrote one before this RFC), so no
  existing data changes meaning; a reader that doesn't yet know about `Silkscreen` still
  tolerates it as an unrecognized-but-structurally-valid enum value per the existing
  unknown-minor-field tolerance policy (`docs/format-spec.md`).
- **C ABI:** additive only (`PCBIR_LAYER_KIND_SILKSCREEN`), mirroring `EdgeCuts`'s own
  ABI addition in RFC 0002.
- **MCP service contracts:** unaffected (not yet built).

## Translation-fidelity impact

Enables `SilkscreenGraphic` import/export to be classified **Preserved** rather than
**Unsupported**, closing the gap RFC 0003 had to leave open pending this decision.

## Open questions

None -- this is a narrow, fully-specified addition. Mask/paste/courtyard/fabrication layer
identity remains an open question for whenever `MaskOpening` or similar entities enter a
future importer pass's in-scope set.
