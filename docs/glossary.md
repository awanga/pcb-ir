# PCB-IR Glossary

> Status: stub.

Canonical PCB-IR terms, mapped to the equivalent terminology used by KiCad, Altium, ODB++,
and IPC-2581, so cross-tool terminology differences don't become a source of confusion.

## Purpose

EDA tools use different words for the same concept (e.g. "net class" vs. "signal class")
and, more subtly, the same word for different concepts. This glossary establishes one
canonical term per concept and cross-references the vendor terms it replaces.

## Layer 1 — Physical Geometry

(To be expanded: pad, via, track/trace, copper pour/region, keepout, drill hit, mask
opening, silkscreen graphic.)

## Layer 2 — Connectivity Graph

(To be expanded: net, pin, bus, differential pair.)

## Layer 3 — Constraint System

(To be expanded: clearance, impedance, DFM rule, assembly constraint.)

## Layer 4 — Stackup Model

(To be expanded: dielectric, copper layer, HDI, blind/buried/microvia.)

## Layer 5 — Process/Optimization Metadata

(To be expanded: provenance, congestion map, cost field.)

## Layer 6 — Intent & Planning

Engineering intent and process, distinct from board state (Layers 1–5) — see
`docs/architecture.md` → "The Intent & Planning layer". Not a per-vendor concept to cross-map
the way Layers 1–5 are (no mainstream EDA board format has an equivalent first-class layer);
terms here are PCB-IR's own.

- **Intent** — a semantic request to change the board (e.g. "route this net"), the input to
  the intent compilation pipeline.
- **Transformation plan** — a concrete, deterministic, not-yet-applied sequence of edits
  produced by planning an intent.
- **Transaction** — a previewable, committable unit produced from a transformation plan;
  carries proposed changes, diagnostics, estimated impact, affected regions, rollback
  information, and provenance.
- **Provenance** — the attributable record (human, agent, transaction) of who/what made a
  decision and why.
- **Semantic identifier** — a namespaced, human-readable, stable name (`net.USB_D+`,
  `component.U15`) resolvable to an `EntityId`, distinct from the internal `EntityId`/
  `Handle` pair (`docs/architecture.md` → "Stable semantic identifiers").

## MCP and AI-interface terms

- **MCP (Model Context Protocol)** — the standardized protocol PCB-IR's AI interface is built
  on; see `docs/mcp/`.
- **Service** — a logical grouping of semantic MCP operations (Design, Query, Routing,
  Analysis, Optimization, Visualization, Dataset).
- **Capability negotiation** — a client/reader querying what a server/file/importer supports
  before depending on it; one shared model across files, MCP, and import/export
  (`docs/architecture.md` → "Capability negotiation").
