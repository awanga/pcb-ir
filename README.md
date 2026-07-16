# PCB-IR

PCB-IR is an open intermediate representation framework for PCB physical design and EDA interoperability.

The project treats PCB design as a persistent semantic physical-design graph rather than a vendor-specific board file.

PCB-IR is inspired by compiler infrastructures such as LLVM:
- deterministic transformation passes
- reusable optimization infrastructure
- composable analysis systems
- extensible semantics
- interoperable tooling

## Goals

PCB-IR aims to make:
- routers pluggable
- analysis composable
- interoperability practical
- visualization unified
- machine learning feasible
- optimization reusable

## Design Principles

- Integer geometry only
- Immutable snapshots with transactional workspaces
- Explicit semantic layering
- Deterministic transformations
- Extensible typed schemas
- Incremental scalable processing
- Backwards-compatible schema evolution
- Vendor-neutral open infrastructure

## Core Semantic Layers

1. Physical Geometry
2. Connectivity Graph
3. Constraint System
4. Stackup Model
5. Optimization/Process Metadata

## Status

Early architecture and schema development. The first release is an interchange-first MVP
(core model, geometry, connectivity, stackup, serialization, and a KiCad round-trip) — the
first slice toward the full vision above, not a reduced scope.

## License

Apache-2.0
