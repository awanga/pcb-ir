# PCB-IR MCP Subsystem

> Status: stub — architectural contract fixed now (this document + `docs/architecture.md` →
> "The MCP subsystem"); per-service operation schemas are filled in as `TASKS.md` Phases
> 14–19 land. This document is normative for *shape* (services, philosophy, capability
> model) ahead of full implementation, the same way `docs/format-spec.md` fixed the wire
> format's shape before every table had a golden byte example.

MCP (Model Context Protocol) is PCB-IR's standardized, public API for AI systems — the layer
through which agents (general-purpose assistants, coding agents, and specialized EDA
copilots) interact with a board. It has the same standing as the C ABI (`docs/c-abi.md`) and
the file format (`docs/format-spec.md`) as a stable integration surface: designed, reviewed,
and versioned with the same rigor, not treated as an internal convenience layer that can
change freely.

See `docs/architecture.md` → "The MCP subsystem" for how this fits the rest of the
architecture (the intent compilation pipeline, transactions, capability negotiation,
multi-agent orchestration). This document is the service-level reference.

## Philosophy: semantic operations, not raw IR

The MCP interface never exposes low-level IR mutation. An agent manipulates **engineering
intent**; PCB-IR's planner and validator are responsible for turning that intent into
well-formed IR edits.

| Never exposed | Exposed instead |
|---|---|
| `modify_node` | `place_component`, `optimize_placement` |
| `create_polygon` | `place_component`, `add_component` |
| `edit_vertex` | `route_net`, `reroute_region` |

If a proposed operation reads like a graph-API method name rather than an engineering action
a PCB designer would describe in a design review, it belongs one layer down, inside a
service's implementation — not on the MCP surface itself.

## Every modifying operation is a compiled intent

Every operation below that changes the board (every service except Query, and every
Analysis/Visualization operation, which are read-only) enters the intent compilation pipeline
(`docs/architecture.md`):

```
Intent → Planning → Transformation Plan → Validation → Preview → Transaction → Commit
  → Diagnostics → Updated Snapshot
```

An MCP call does not, itself, commit anything. It produces a **transaction** — proposed
changes, diagnostics, estimated DRC/SI impact, affected regions, rollback information, and
provenance — that the caller (an agent, or a human reviewing the agent's proposal) previews
and then explicitly commits or discards.

## Service taxonomy

### Design

Board/stackup/net/component authoring.

| Operation | Kind |
|---|---|
| create board | intent → transaction |
| create stackup | intent → transaction |
| create net | intent → transaction |
| add component | intent → transaction |
| update component | intent → transaction |

### Query

Read-only introspection. Query operations bypass the intent pipeline entirely — there is
nothing to plan, validate, preview, or commit.

| Operation | Kind |
|---|---|
| query nets | read-only |
| query constraints | read-only |
| query geometry | read-only |
| query regions | read-only |
| query stackup | read-only |

### Routing

| Operation | Kind |
|---|---|
| route net | intent → transaction |
| reroute region | intent → transaction |
| fanout BGA | intent → transaction |
| optimize differential pair | intent → transaction |

### Analysis

Verification and estimation. Analysis operations are read-only/estimation-only — none
produces a transaction, since none changes the board.

| Operation | Kind |
|---|---|
| run DRC | read-only |
| run DFM | read-only |
| estimate impedance | read-only |
| evaluate skew | read-only |
| identify congestion | read-only |

### Optimization

| Operation | Kind |
|---|---|
| optimize placement | intent → transaction |
| optimize routing | intent → transaction |
| reduce vias | intent → transaction |
| optimize return paths | intent → transaction |

### Visualization

Pure reads over a snapshot; never requires a transaction.

| Operation | Kind |
|---|---|
| render board | read-only |
| render region | read-only |
| highlight objects | read-only |
| extract render scene | read-only |

### Dataset

| Operation | Kind |
|---|---|
| export graph | read-only |
| extract ML features | read-only |
| capture snapshot | read-only |
| replay snapshot | deterministic replay (see `docs/architecture.md` → "Machine learning infrastructure") |

## Capability negotiation

A server advertises which capabilities it supports (`routing`, `geometry`, `constraints`,
`timing`, `RF`, `ML`, `DFM`, `SI`, `visualization`, …) before any operation is called. A
client queries this before depending on a capability and degrades gracefully — e.g. skipping
an SI-impact estimate on a server without the `SI` capability — rather than failing outright.
This is the same shared model (`libs/capabilities`) used by import/export capability
advertisement; see `docs/architecture.md` → "Capability negotiation".

## Multi-agent use

Cooperating agents (e.g. Planning → Placement → Routing → Signal Integrity → DFM →
Manufacturing → Verification) communicate only through immutable snapshots and transactions —
never shared mutable state. See `docs/architecture.md` → "Multi-agent architecture".

## Stable references

MCP operations address board entities by [semantic identifier](../architecture.md#stable-semantic-identifiers)
(`net.USB_D+`, `component.U15`, `region.rf_frontend`, …), never by raw `EntityId` or in-memory
handle. Renaming an entity preserves the old identifier as a resolvable alias.

## Vendor neutrality

The reference server (`apps/mcp-server`, `TASKS.md` Phase 18) implements the MCP
specification only — no protocol extensions specific to any one AI platform. ChatGPT, Claude,
Gemini, Cursor, GitHub Copilot, and any other MCP-capable client get the same interface.
