# PCB-IR Conformance

> Status: stub.

Conformance is how third parties gain confidence to integrate PCB-IR (the glTF / SPIR-V
lesson). It has two parts: a **validator** and a **golden-file corpus**.

## Validator

- `pcb-ir validate <file>` checks a file against `docs/format-spec.md`.
- Built on the same code path the reference reader uses, so "valid" means "loads in the
  reference reader."
- Emits stable, documented diagnostic codes.

## Golden-file corpus

- Lives under `tests/corpus/`.
- Each entry records the source tool/version (e.g. the pinned KiCad version).
- Good files must pass; seeded-bad files must fail with the expected code.

## Round-trip fidelity

- The headline adoption metric: `import → IR → export → re-import` must be semantically equal
  (nets, layers, geometry). Any non-preserved attribute is reported by the lossiness report
  with a reason.
- Import/export is treated as compilation, not file conversion (`docs/architecture.md` →
  "Import/export as compilation"): importing is a frontend compiling into PCB-IR, exporting
  is a backend generating vendor output from it. Both directions report **translation
  fidelity**.

## Translation-fidelity classification

Every import/export operation classifies each piece of source/target semantics into exactly
one of four tiers — this is a central design philosophy, not a cosmetic label on the
lossiness report:

| Tier | Meaning |
|---|---|
| **Preserved** | Carried through exactly; round-trippable with no information loss. |
| **Approximated** | Carried through with a documented, bounded loss (e.g. a spline flattened to arcs/segments per the tolerance in `docs/format-spec.md`). |
| **Lost** | Existed in the source, has no PCB-IR representation, and is dropped — reported with a reason, never silently discarded. |
| **Unsupported** | PCB-IR could represent it, but this particular importer/exporter doesn't yet handle it — distinct from **Lost**, which is a representational gap rather than an implementation gap. |

The lossiness report is this classification applied to a specific board: a per-attribute
list of which tier it fell into and why. A **Preserved**-only report is the ideal outcome for
a corpus board; **Lost**/**Unsupported** entries are expected for esoteric vendor features and
must never be silently absent from the report.

This classification is also what an importer/exporter advertises through
[capability negotiation](architecture.md#capability-negotiation) before a caller depends on a
given format pair's fidelity.

(To be expanded with the diagnostic code table.)
