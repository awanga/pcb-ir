# PCB-IR

PCB-IR is a vendor-neutral, open, extensible intermediate representation platform for PCB
physical design — and the semantic foundation for an open physical-design ecosystem.

The project treats PCB design as a persistent semantic physical-design graph rather than a
vendor-specific board file. That graph, not any one tool's file format, is what PCB-IR
actually is: a stable IR, a set of deterministic transformation passes over it, and the
reusable analysis, optimization, visualization, and AI infrastructure built on top.

PCB-IR is inspired by compiler infrastructures such as LLVM:
- deterministic transformation passes,
- reusable analysis infrastructure,
- reusable optimization infrastructure,
- composable, extensible semantics,
- interoperable frontends (importers) and backends (exporters).

Read that analogy as load-bearing, not decorative: importing a board **is** compiling it into
PCB-IR, and exporting one **is** generating code from it — see `docs/architecture.md` for
what that means for translation fidelity.

## What the project actually consists of

PCB-IR is not solely an intermediate representation (though that remains a primary,
non-negotiable design goal). It is:

1. a **persistent physical-design IR**,
2. **deterministic, compiler-style transformation passes**,
3. **reusable analysis infrastructure**,
4. **reusable optimization infrastructure**,
5. **visualization infrastructure**,
6. **import/export infrastructure**,
7. **machine learning infrastructure**,
8. a **standardized AI interface** — [MCP](#mcp-the-standardized-ai-interface), and
9. a candidate **long-term interoperability standard** for PCB physical design.

## MCP: the standardized AI interface

The project includes a first-class **MCP (Model Context Protocol) subsystem** — the public
API through which AI systems interact with PCB-IR. This is not an optional integration or a
convenience wrapper; it holds the same standing as the C ABI and the file format as a stable
integration surface (`docs/architecture.md` → "The MCP subsystem").

The MCP interface is deliberately **semantic**, not structural: an agent never issues "modify
node" or "edit vertex" — it issues **Place component**, **Route net**, **Optimize
differential pair**, **Fanout BGA**, **Run DRC**, **Estimate impedance**, **Extract ML
dataset**. AI manipulates engineering intent, never raw IR. Every modifying request is
compiled — through an explicit Intent → Planning → Transformation Plan → Validation →
Preview → Transaction → Commit → Diagnostics pipeline — into the same deterministic edits the
core library already exposes, and every transaction is previewable before it's committed. See
`docs/architecture.md` and `docs/mcp/` for the full model.

This makes PCB-IR the layer through which ChatGPT, Claude, Gemini, Cursor, GitHub Copilot,
and future agentic platforms interact with a board deterministically, vendor-neutrally, and
reproducibly — the same interface for every agent, rather than each integration inventing its
own bridge into a vendor's proprietary format.

## Goals

PCB-IR aims to make:
- routers pluggable,
- analysis composable,
- interoperability practical,
- visualization unified,
- machine learning feasible and reproducible,
- optimization reusable, and
- AI-assisted design deterministic and vendor-neutral, by construction rather than by
  integration effort.

## Design Principles

- Integer geometry only.
- Immutable snapshots with transactional workspaces.
- Explicit semantic layering, including intent — see below.
- Deterministic transformations, replayable and previewable.
- Extensible typed schemas.
- Incremental, scalable processing.
- Backwards-compatible schema evolution.
- Vendor-neutral open infrastructure.
- AI agents manipulate intent, never raw IR — see `docs/architecture.md` → "The MCP
  subsystem".

## Core Semantic Layers

1. Physical Geometry
2. Connectivity Graph
3. Constraint System
4. Stackup Model
5. Optimization/Process Metadata
6. Intent & Planning — engineering intent, plans, provenance, and AI reasoning, distinct from
   board state (Layers 1–5); see `docs/architecture.md`.

## Status

Early architecture and schema development. The first release is an interchange-first MVP
(core model, geometry, connectivity, stackup, serialization, and a KiCad round-trip) — the
first slice toward the full vision above, not a reduced scope. The MCP subsystem, intent
layer, and ML infrastructure described above are architected now and sequenced as Post-MVP
work, so v0.1's schema never needs a breaking change to accommodate them later.

## Documentation

- `docs/architecture.md` — the full architecture, including the MCP subsystem, the intent
  compilation pipeline, and the long-term positioning of the project.
- `docs/format-spec.md` — the on-disk schema and versioning policy.
- `docs/mcp/` — MCP service taxonomy and philosophy.
- `PRD.md` — product requirements.
- `AGENTS.md` — engineering principles and AI-contributor guidance.

## License

Apache-2.0
