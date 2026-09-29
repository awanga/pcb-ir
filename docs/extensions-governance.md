# PCB-IR Extensions & Governance

> Status: stub.

Extensions let tools add typed, versioned data without forking the format. The model follows
the Khronos pattern (vendor / EXT / registered tiers) so the ecosystem stays interoperable.

## Namespace tiers

- **`VENDOR_*`** — single-vendor, no coordination required; may change freely.
- **`EXT_*`** — multi-vendor, agreed between two or more implementers.
- **Registered (`PCBIR_*`)** — promoted, stable, documented in this repo.

## Rules

- Extensions are typed and versioned.
- Readers must safely ignore unknown extensions (forward compatibility).
- Capability negotiation: a file declares the extensions it uses; a reader declares the
  extensions it supports; unsupported-but-required extensions are a documented error. This is
  the file-format instance of a single shared capability-negotiation model
  (`docs/architecture.md` → "Capability negotiation") also used by the MCP subsystem
  (`docs/mcp/`) and import/export pipelines (`docs/conformance.md`).

## Registry

A third party must be able to register / namespace an extension from this document alone.
(Registration process to be added.)

| Namespace/name | Version | Domain | Purpose |
|---|---|---|---|
| `PCBIR_KICAD/pad_shape` | 1 | Geometry (keyed by a `Pad` entity) | Carries a KiCad-imported Pad's original parametric shape (rect/circle/oval/roundrect/trapezoid, plus its absolute rotation) losslessly alongside the always-present geometric outline, so a reader that understands it reconstructs the exact original KiCad pad primitive (Preserved) instead of falling back to a generic `custom` shape (Approximated) -- `docs/rfcs/0003-kicad-importer-exporter.md`'s "Pad shape fidelity". Fixed 56-byte little-endian payload; see `src/pcbir/kicad/pad_shape_extension.hpp` for the exact layout. |
