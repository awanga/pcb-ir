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
  extensions it supports; unsupported-but-required extensions are a documented error.

## Registry

A third party must be able to register / namespace an extension from this document alone.
(Registration process and the registered-extension table to be added.)
