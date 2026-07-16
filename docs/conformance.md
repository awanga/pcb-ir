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

(To be expanded with the diagnostic code table.)
