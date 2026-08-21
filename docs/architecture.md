# PCB-IR Architecture

## Overview

PCB-IR represents a PCB as a persistent semantic physical-design graph across six explicit
layers: geometry, connectivity, constraints, stackup, process/optimization metadata, and
intent/planning. That graph — not any single vendor's board file — is the project's actual
subject matter.

PCB-IR is built on the same premise LLVM applied to compilers: a stable, well-specified
intermediate representation lets independent frontends (importers), backends (exporters),
and middle-end passes (analysis, optimization, validation) interoperate without ever needing
to agree with each other directly. Concretely, the project is:

- a **persistent physical-design IR** (this document, `docs/format-spec.md`),
- **deterministic, compiler-style transformation passes** over that IR,
- **reusable analysis infrastructure** (queries, diagnostics, spatial indexing),
- **reusable optimization infrastructure** (Post-MVP pass manager, routing/placement passes),
- **visualization infrastructure** decoupled from IR internals,
- **import/export infrastructure**, treated as compiler frontends/backends (see
  [Import/export as compilation](#importexport-as-compilation)),
- **machine learning infrastructure** operating on immutable snapshots,
- a **standardized AI interface** — the MCP subsystem (see [The MCP subsystem](#the-mcp-subsystem)) — and
- a candidate **long-term interoperability standard** for PCB physical design.

None of these are bolted on to an otherwise format-only project. The file format and the C
ABI remain the stable integration surface (`docs/format-spec.md`, `docs/c-abi.md`), but the
surrounding infrastructure — up to and including how AI systems interact with a board — is
part of the architecture from v0.1 onward, even where its implementation lands as Post-MVP
work.

PCB-IR positions itself as a **vendor-neutral physical-design platform**, not merely a file
format. See [Long-term positioning](#long-term-positioning) for the closing statement of that
ambition.

## Semantic layers

| Layer | Owns | Independent of |
|---|---|---|
| 1. Geometry | shapes, primitives, typed board entities | connectivity |
| 2. Connectivity | nets, pins, diff pairs, buses | geometry |
| 3. Constraints | clearance/impedance/timing/DFM rules (Post-MVP evaluators) | — |
| 4. Stackup | materials, layers, vias | — |
| 5. Metadata | provenance, process, ML features | — |
| 6. Intent & Planning | engineering intent, plans, provenance of decisions, AI reasoning | geometry, connectivity |

Layers 1–5 describe **what the board is**. Layer 6 describes **why it is that way and what
should happen next** — it is documented in its own section,
[The Intent & Planning layer](#the-intent-planning-layer-layer-6).

Two existing uses of the word "intent" elsewhere in this doc set are deliberately *not* the
same concept as Layer 6, and the distinction matters:

- **Electrical intent** (differential pairs, bus membership/ordering) is *structural* — it is
  part of Connectivity (Layer 2), because it's data a router or DRC pass must consult to do
  its job, not a record of reasoning about that data.
- **Manufacturing intent** (DFM/assembly rules, process capability) is *structural* in the
  same way — it lives in Stackup (Layer 4, process capability) and the Constraint System
  (Layer 3, DFM/assembly rules, Post-MVP).

Layer 6 is different in kind: it never changes what a board *is* (no pass reads Layer 6 to
decide geometry or connectivity correctness). It records the **process** around those
decisions — goals, rationale, alternatives considered, open work, and who or what (human or
AI) decided what and why. A board with an empty Layer 6 is still a completely valid,
completely determinate board; Layer 6 is provenance and planning context, not board state.

## Snapshots and workspaces

- **Immutable snapshot** — the canonical state, backed by a zero-copy, mmap-able FlatBuffers
  buffer. Read-only; no mutation API.
- **Transactional workspace** — a separate mutable overlay. Edits accumulate in the workspace
  and a `commit` produces a new immutable snapshot. The base snapshot is never modified;
  structural sharing is used where possible.

This separation exists because FlatBuffers buffers are immutable once built. It is also the
foundation the [intent compilation pipeline](#the-intent-compilation-pipeline) and
[multi-agent architecture](#multi-agent-architecture) build on: every richer mechanism
described below (previewable transactions, cooperating agents) is additional structure around
this same snapshot/workspace primitive, never a competing one.

## Memory model

- Arena allocation for snapshot-owned objects; typed, generation-checked handles instead of
  raw pointers.
- ECS-like composition: an entity is a typed handle; its data lives in separate typed
  component tables (geometry, connectivity, stackup reference, metadata), not a deep class
  hierarchy. This keeps layers independently extensible and cache-friendly.

## Identity & versioning

- **`EntityId`** — a stable, monotonically assigned 64-bit identity for a logical entity. It
  is never reused and is carried across every future commit for that entity's lifetime; it is
  what the serialized schema and cross-layer references (e.g. a via naming a stackup layer)
  use. Distinct from a `Handle`, which is an arena-slot reference.
- **`Handle<Tag>`** — an arena-local, generation-checked (index, generation) pair used for
  fast in-memory lookup and use-after-erase detection. A handle is not guaranteed to survive
  arena reorganizations (e.g. slot reuse after an erase) the way an `EntityId` is; call
  `Arena::find(EntityId)` to recover a fresh handle when needed.
- **`SnapshotVersion`** — a monotonically increasing counter on the snapshot lineage,
  incremented by one on every `Workspace::commit()`.

### Stable semantic identifiers

`EntityId` is stable but opaque — a 64-bit integer is not something a human, a review
comment, or an AI agent should have to quote back verbatim. PCB-IR additionally defines
**semantic identifiers**: namespaced, human-readable, dotted names resolvable to an
`EntityId`, e.g. `board.main`, `stackup.signal1`, `component.U15`, `net.USB_D+`,
`constraint.diffpair.usb`, `region.rf_frontend`.

- A semantic identifier is **not** a replacement for `EntityId` — it is a resolvable alias
  layer on top of it, maintained as an explicit name→`EntityId` table (an *alias table*), not
  derived implicitly from other fields (an entity's designator can change; its semantic
  identifier's resolution target should not silently follow it without a recorded rename).
- **Renames produce aliases, not breakage.** Renaming `component.U15` to `component.U15A`
  leaves `component.U15` resolvable as an alias pointing at the same `EntityId`, so external
  references (an open MCP transaction, a saved review comment, a training-dataset label)
  keep working across the rename. The rename itself — old name, new name, who/what renamed
  it and why — is recorded as a Layer 6 provenance entry (see below), not silently discarded.
- Semantic identifiers are namespaced by entity kind (`board.`, `stackup.`, `component.`,
  `net.`, `constraint.`, `region.`, …), mirroring the canonical-term namespacing already
  established in `docs/glossary.md`.
- This is the naming layer the [MCP subsystem](#the-mcp-subsystem) and
  [intent transactions](#intent-transactions) are specified against: an AI agent references
  `net.USB_D+`, never a raw `EntityId` or arena handle.

## Determinism

No floating point in serialized output. Canonical element ordering and stable iteration are
invariants verified by a determinism test suite. Building the same logical board
twice must yield byte-identical snapshots.

Determinism is not only a serialization property — it is what makes
[intent transactions](#intent-transactions) previewable and replayable, what makes
[ML training reproducible](#machine-learning-infrastructure), and what makes a
[multi-agent](#multi-agent-architecture) pipeline auditable: the same intent, applied to the
same snapshot, must produce the same transformation plan and the same resulting snapshot,
regardless of which agent, host, or platform ran it.

## The Intent & Planning layer (Layer 6)

Layer 6 is a versioned, persistent record of engineering intent, distinct from the board
state described by Layers 1–5. It captures things a board file has traditionally had no
place to put:

- routing objectives and optimization goals,
- placement rationale,
- manufacturing goals,
- human decisions and their reasoning,
- AI reasoning (the justification an agent recorded for a proposed change),
- unresolved design tasks,
- optimization alternatives considered and rejected,
- review comments.

Like every other layer, Layer 6 is carried in the snapshot, versioned with
`SnapshotVersion`, and round-trips through `Workspace::commit()`. It supports **provenance**:
every Layer 6 entry is attributable (which human, which agent, which transaction produced
it) and timestamped, so a board's design history is queryable the same way its geometry is.
Layer 6 entries are additive/append-friendly by convention — superseding a goal or resolving
a task adds a new entry referencing the old one, rather than mutating history in place —
which keeps the provenance trail intact under the same append-only spirit as
`DiagnosticCode` (`docs/format-spec.md`) and the extension namespace
(`docs/extensions-governance.md`).

Layer 6 is what makes the [intent compilation pipeline](#the-intent-compilation-pipeline) and
[MCP subsystem](#the-mcp-subsystem) more than a stateless request/response API: a planning
agent's rationale, a rejected routing alternative, or a human's "leave this via pattern
alone, it's intentional" comment all persist as first-class, queryable board data.

## The intent compilation pipeline

AI and MCP requests never edit the IR directly (see
[MCP philosophy](#mcp-philosophy-semantic-operations-not-raw-ir)). Instead, every modifying
request is **compiled**, in the same sense a compiler compiles source to machine code:

```
Intent
  ↓
Planning
  ↓
Transformation Plan
  ↓
Validation
  ↓
Preview
  ↓
Transaction
  ↓
Commit
  ↓
Diagnostics
  ↓
Updated Snapshot
```

- **Intent** — a semantic request ("optimize this differential pair", "fanout this BGA"),
  expressed through an MCP service call or the `apps/intent-planner` CLI, and recorded as a
  Layer 6 entry.
- **Planning** — the intent is expanded into one or more candidate approaches. This is where
  a routing/placement/optimization pass decides *how* to satisfy the intent; it is the only
  stage that may be non-deterministic in its *search* (e.g. trying several placements), but
  it must resolve to a deterministic, fully-specified plan before the next stage.
- **Transformation Plan** — a concrete, deterministic sequence of IR-level edits (the same
  primitive edits a `Workspace` already supports), not yet applied.
- **Validation** — the plan is checked against constraints, geometry validity, and
  connectivity invariants before anything is written.
- **Preview** — the plan is applied to a *scratch* workspace overlay (never the canonical
  lineage) so its effects can be inspected before commit. This reuses the existing
  [snapshot/workspace](#snapshots-and-workspaces) mechanism; a preview is nothing more than a
  workspace overlay whose commit is deferred and optional.
- **Transaction** — see [Intent transactions](#intent-transactions) below.
- **Commit** — the same `Workspace::commit()` primitive already described above; produces a
  new immutable snapshot.
- **Diagnostics** — the same unified `DiagnosticCode` space (`docs/format-spec.md`) every
  other validation pass reports into.
- **Updated Snapshot** — the new canonical state, ready to be handed to the next stage of a
  [multi-agent](#multi-agent-architecture) pipeline or back to the requesting caller.

This pipeline is the project's compiler model applied reflexively to itself: an intent is
"source", a transformation plan is "IR-to-IR lowering", and a commit is "codegen" against the
snapshot's own binary representation.

## Intent transactions

Every modifying intent operation executes as a transaction. A transaction is the unit the
[preview stage](#the-intent-compilation-pipeline) produces and the unit a caller commits or
discards; it always carries:

- the **proposed changes** (the transformation plan, in inspectable form),
- **diagnostics** (from the unified `DiagnosticCode` space),
- **estimated DRC impact** and **estimated SI impact** (best-effort, explicitly labeled as
  estimates — analogous to the arc-flattening caveat in `docs/format-spec.md`: an estimate is
  not the same guarantee as the exact validation a full DRC/SI pass performs),
- the **affected regions** (so a caller — human or agent — can scope review to what actually
  changed, rather than diffing the whole board),
- **rollback information** (sufficient to discard the transaction with no trace on the base
  snapshot — trivial today, since a discarded preview overlay simply never commits),
- **provenance** (which intent, which agent/human, which planning rationale produced this
  transaction — recorded as Layer 6 data once committed).

A transaction is **previewable before commit**: a caller can inspect all of the above and
choose not to commit, with zero effect on the canonical snapshot lineage. This is the
mechanism that lets an AI agent (or a human reviewer) evaluate "what would routing this net
actually do to the board" before it happens.

## Capability negotiation

`docs/extensions-governance.md` already establishes that a file declares the extensions it
uses and a reader declares the extensions it supports. Capability negotiation generalizes
that same principle to every surface that can vary in what it supports:

- an **MCP server** advertises which services and operations it implements (routing,
  geometry, constraints, timing, RF, ML, DFM, SI, visualization, …);
- an **importer/exporter** advertises which semantics it can preserve for a given source/
  target format (see [Import/export as compilation](#importexport-as-compilation));
- a **reader** advertises which schema extensions and wire-format minor versions it
  understands (the existing `docs/extensions-governance.md` model).

In every case, a caller queries capabilities before depending on them, and an agent or tool
adapts to what's actually available rather than assuming a fixed surface. This is
implemented once, as shared infrastructure (`libs/capabilities`, see
[Repository layout](#repository-layout)), and consumed by the MCP subsystem, the
import/export pipelines, and the file-format reader alike, rather than three independent
ad-hoc mechanisms.

## Extensibility mechanism

Two schema-level escape hatches exist from v0.1 onward so future/esoteric cases (RF
metadata, flex-PCB, embedded components, chiplets/interposers, photonics) never require a
breaking schema change:

- **Namespaced typed-extension table** (`VENDOR_*`/`EXT_*`/`PCBIR_*`, see
  `docs/extensions-governance.md`) — attachable to any entity; unknown extensions are
  safely ignored by readers that don't understand them.
- **Opaque passthrough blob** — per-entity raw bytes for source-format data an importer
  recognizes syntactically but can't semantically map. Distinct from the typed-extension
  table: this is un-interpreted, round-tripped verbatim.

## The MCP subsystem

MCP (Model Context Protocol) is not "another tool" bolted onto PCB-IR — it is **the public
API over PCB-IR for AI systems**, with the same standing as the C ABI and the file format as
a first-class integration surface. Where the C ABI is how a program links against PCB-IR,
MCP is how an agent — ChatGPT, Claude, Gemini, Cursor, GitHub Copilot, or a future agentic
platform — interacts with it: deterministically, vendor-neutrally, and reproducibly. Full
service definitions live in `docs/mcp/`; this section covers the architectural commitments.

### MCP philosophy: semantic operations, not raw IR

The MCP interface never exposes low-level IR mutation. An agent manipulates **engineering
intent**, not graph nodes. Concretely:

| Not exposed | Exposed instead |
|---|---|
| modify node | Optimize placement |
| create polygon | Place component |
| edit vertex | Route net |
| — | Optimize differential pair |
| — | Fanout BGA |
| — | Improve return path |
| — | Reduce congestion |
| — | Run DRC / Run DFM |
| — | Estimate impedance |
| — | Generate visualization |
| — | Extract ML dataset |
| — | Query constraints |

Every operation on the right compiles down through the
[intent compilation pipeline](#the-intent-compilation-pipeline) to the same primitive edits
the C++ workspace API already supports — the MCP surface adds a semantic layer on top of the
existing mutation primitives, it does not add a second, independent way to mutate a board.

### MCP service organization

Rather than exposing a flat list of hundreds of unrelated tools, the MCP interface is
organized into logical services, each owning a coherent slice of semantic operations:

| Service | Example operations |
|---|---|
| **Design** | create board, create stackup, create net, add component, update component |
| **Query** | query nets, query constraints, query geometry, query regions, query stackup |
| **Routing** | route net, reroute region, fanout BGA, optimize differential pair |
| **Analysis** | run DRC, run DFM, estimate impedance, evaluate skew, identify congestion |
| **Optimization** | optimize placement, optimize routing, reduce vias, optimize return paths |
| **Visualization** | render board, render region, highlight objects, extract render scene |
| **Dataset** | export graph, extract ML features, capture snapshot, replay snapshot |

Full per-service operation contracts (inputs, outputs, capability requirements) are specified
in `docs/mcp/`. Query-service operations are read-only and bypass the intent pipeline
entirely (there is nothing to compile or commit); every other service's modifying operations
go through it.

### Multi-agent architecture

The MCP subsystem is designed to support cooperating, specialized agents rather than a single
monolithic one, e.g.:

```
Planning Agent
  ↓
Placement Agent
  ↓
Routing Agent
  ↓
Signal Integrity Agent
  ↓
DFM Agent
  ↓
Manufacturing Agent
  ↓
Verification Agent
```

Agents in such a pipeline communicate **only** through immutable snapshots and transactions —
never shared mutable state. This is not a new mechanism; it is a direct consequence of the
[snapshots-and-workspaces](#snapshots-and-workspaces) model already at the foundation of the
project: a snapshot handed from a Placement Agent to a Routing Agent is the same kind of
object a single-threaded caller already works with, so a cooperating-agents pipeline adds no
new class of state to reason about, only more callers of the same primitive.

### Capability-aware agents

Per [Capability negotiation](#capability-negotiation), an agent queries the MCP server's
advertised capabilities (`routing`, `geometry`, `constraints`, `timing`, `RF`, `ML`, `DFM`,
`SI`, `visualization`, …) before assuming an operation is available, and degrades gracefully
(e.g. skipping an SI-impact estimate on a server that hasn't loaded the SI capability) rather
than failing outright.

## Machine learning infrastructure

ML operates on immutable IR snapshots, never on live mutable state — the same isolation
principle that underlies the multi-agent architecture applies to training and inference. The
ML subsystem (`libs/ml`) supports:

- **stable feature extraction** — features keyed by [semantic identifiers](#stable-semantic-identifiers)
  and `EntityId`s, so a feature vector remains attributable to the same logical entity across
  snapshots;
- **graph embeddings** over the connectivity/geometry graph;
- **snapshot replay** — a recorded sequence of committed transactions can be replayed
  deterministically to reconstruct any intermediate snapshot, which is what makes training
  data reproducible;
- **routing and placement datasets**, extracted from snapshot history;
- **policy evaluation** and **candidate generation** — an ML policy proposes a transformation
  plan the same way a human- or heuristic-driven planning stage would, entering the
  [intent compilation pipeline](#the-intent-compilation-pipeline) at the Planning stage
  rather than bypassing Validation/Preview;
- **deterministic validation** of ML-proposed plans, using the same Validation stage every
  other intent goes through — an ML policy gets no special exemption from constraint or
  geometry checks;
- **accepted-transaction recording** — every committed transaction produced by an ML policy
  is retained (via Layer 6 provenance) as training signal for future policy iterations.

Because snapshot replay and transaction recording are deterministic, training is
reproducible: the same recorded transaction history regenerates the same dataset.

## Import/export as compilation

**Import is compilation. Export is code generation.** An importer is a frontend: it parses a
vendor format and lowers it into PCB-IR, the same way a compiler frontend lowers source text
into IR. An exporter is a backend: it generates vendor-format output from PCB-IR, the same
way a compiler backend generates machine code from IR. Neither direction is "just a file
converter" — both are compilation passes, and both are held to compilation-pass standards:
determinism, and honest reporting of what could and couldn't be preserved.

Every import/export operation reports **translation fidelity**, classified into four tiers
(the full diagnostic model and lossiness-report format are specified in
`docs/conformance.md`):

- **preserved** — semantics carried through exactly, round-trippable;
- **approximated** — semantics carried through with a documented, bounded loss (e.g. a
  spline flattened to arcs/segments per `docs/format-spec.md`);
- **lost** — semantics that existed in the source and have no PCB-IR representation, dropped
  and reported, not silently discarded;
- **unsupported** — semantics PCB-IR could represent but this particular importer/exporter
  doesn't yet handle, distinct from genuinely unrepresentable ("lost") semantics.

This four-tier model, together with [capability negotiation](#capability-negotiation),
applies uniformly to every importer/exporter PCB-IR ships (KiCad first; IPC-2581/ODB++/
Gerber-X2 Post-MVP) and to any third-party importer/exporter built against the same
infrastructure.

## Spatial indexing

`pcbir::geometry::SpatialIndex` (`include/pcbir/geometry/spatial_index.hpp`) is a uniform
grid over `BBox`es, used for region queries (e.g. "which primitives overlap this rectangle").
It is a query-time-only auxiliary structure, not part of any snapshot or the wire format: it
is built from a snapshot's entities after loading, discarded or rebuilt freely, and never
serialized. This is the MVP default per the locked strategy decision (no Boost.Geometry or
other new dependency); Boost.Geometry remains a reserved, opt-in backend candidate only if
profiling later shows the custom grid insufficient at scale, and is never used for
boolean/offset operations, which stay on Clipper2 regardless. Each inserted box is bucketed
into every grid cell it overlaps; a query unions the candidates from every cell the query
region overlaps and then narrows to an exact intersection test, so it always returns the same
set brute force would, never an over-approximation.

## Archive / container layer (reserved, Post-MVP)

The FlatBuffers snapshot above is a single immutable buffer — it has no native notion of
version history, deltas, compression, or embedded blobs. A Post-MVP chunked container is
designed here now so v0.1's schema never needs a breaking change to accommodate it:

- **Format:** glTF-`.glb`-style — a small fixed header + a table-of-contents + a sequence of
  typed chunks (`FullSnapshot`, `Delta`, `Blob`, `Provenance`). Chosen over reusing Zip or
  SQLite as the container: simpler, fully deterministic, no extra runtime dependency beyond
  a compressor, and every chunk type maps directly onto a concept this project already has.
- **Compression:** zstd per chunk (permissive-licensed, fast, ubiquitous).
- **Deltas:** a `Delta` chunk is a *serialized workspace transaction log* (the same edit
  representation the mutable workspace already produces on commit) — not a binary diff of
  two FlatBuffers buffers, which wouldn't be semantically meaningful. This makes the archive
  layer a natural persistence of history that already exists in the runtime model, not a new
  concept. A recorded [intent transaction](#intent-transactions) log and an archive `Delta`
  chunk are the same shape of object for the same reason `snapshot replay` (above) works: the
  runtime already models edits as a replayable log.
- **Blobs / provenance:** embedded original source-format bytes (e.g. the imported
  `.kicad_pcb`) and a provenance chunk recording import/transform history, so "no
  information lost" can be verified against the original file even after PCB-IR round-trips.
  This is also where Layer 6 provenance and translation-fidelity reports are archived
  long-term, once the archive layer is built.

## Repository layout

### Current (v0.1 MVP)

Code today is organized flat, by concern, under `include/pcbir/` and `src/pcbir/`
(`core/`, `geometry/`), plus top-level `schemas/`, `tests/`, `fuzz/` — kept intentionally flat
through the interchange-first MVP push to minimize build-system churn while the format itself
is still stabilizing.

### Target (long-term)

As the subsystems described in this document are built out, the repository grows into a
multi-library layout that gives each subsystem its own versioned, independently buildable
unit:

```
libs/
    core/            — arena, handles, snapshot/workspace primitives
    geometry/         — Layer 1 (primitives, typed entities, boolean ops, spatial index)
    topology/         — Layer 2, Connectivity (net graph, diff pairs, buses) —
                        "topology" is this library's name; "Connectivity" remains the
                        semantic layer's name throughout this doc set
    constraints/      — Layer 3, declarative constraint model + evaluators
    serialization/    — cross-cutting: root snapshot schema, versioning, extension tables
    passes/           — deterministic transformation/optimization pass infrastructure
    visualization/    — render-scene extraction, decoupled from IR internals
    ml/               — Layer 6-adjacent: feature extraction, replay, dataset export
    mcp/              — MCP protocol/transport + tool registration + capability advertisement
    services/         — the semantic service implementations MCP exposes (Design, Query,
                        Routing, Analysis, Optimization, Visualization, Dataset) — protocol-
                        agnostic, so apps/mcp-cli can call them directly without going
                        through the MCP transport
    intent/           — Layer 6 data model, the planner (Intent → Transformation Plan), and
                        the transaction engine (Plan → Validation → Preview → Commit)
    capabilities/     — the shared capability-negotiation model/registry (see
                        [Capability negotiation](#capability-negotiation)), consumed by mcp,
                        services, and import/export alike

apps/
    mcp-server/        — the official MCP server binary (see below)
    mcp-cli/           — a CLI over the same libs/services, for scripting without an agent
    intent-planner/    — standalone planner/transaction-preview tool

docs/
    mcp/               — MCP service taxonomy and philosophy (this doc's MCP section is the
                        architectural summary; docs/mcp/ has full per-service contracts)
    architecture/       — (this document may split into per-topic pages here as it grows)
    rfcs/              — request-for-comments process for major/breaking changes (formalizes
                        the existing AGENTS.md "elevate major changes for approval" rule)
    extensions-governance.md — unchanged; continues to own the extension namespace model
```

`libs/topology`, `libs/constraints`, `libs/passes`, `libs/visualization`, and `libs/ml` are
where the corresponding Post-MVP work (Connectivity is MVP; Constraints, Optimization,
Visualization, ML infrastructure are Post-MVP) land once built — this layout is the
destination, not a renaming of work already done. `libs/mcp`, `libs/services`, `libs/intent`,
and `libs/capabilities` are new subsystems with no prior code to migrate, so they can be
created directly in this layout as soon as that work begins.

**Migrating `include/pcbir/{core,geometry}` and `src/pcbir/{core,geometry}` into
`libs/core`/`libs/geometry` is itself a major build-system change** and, per `AGENTS.md`'s
change-control rules, requires explicit user approval before it's carried out — it is tracked
as its own deliberate task rather than assumed. Until that migration happens, treat this
section as the target this document plans against, and the "Current" subsection above as
ground truth for where code actually lives.

## Long-term positioning

PCB-IR aspires to be:

- the **LLVM of PCB physical design** — a stable IR that decouples frontends (importers),
  backends (exporters), and middle-end passes (analysis, optimization) from one another;
- the **canonical semantic IR** for PCB systems — one representation that distinguishes a via
  from a polygon, a net from a trace, and intent from geometry, so tooling never has to
  rediscover semantics a source format already knew and lost;
- the **canonical AI interface for hardware design** — the [MCP subsystem](#the-mcp-subsystem)
  is that interface, not an optional add-on bolted onto an otherwise AI-unaware format;
- the **interoperability layer between EDA tools** — open and closed-source alike, via the C
  ABI, the file format, and now the MCP subsystem;
- the **execution engine for deterministic optimization passes** — routing, placement, and
  analysis passes that any tool can invoke against the same IR, with the same determinism
  guarantees this document establishes throughout;
- the **semantic substrate for AI-assisted PCB engineering** — the layer through which
  ChatGPT, Claude, Gemini, Cursor, GitHub Copilot, and future agentic platforms interact with
  a board deterministically, vendor-neutrally, and reproducibly, via the same MCP subsystem
  every other agent uses, rather than each integration inventing its own ad hoc, tool-specific
  bridge into a vendor's proprietary format.

Every section above — snapshots, determinism, capability negotiation, the intent pipeline,
the MCP subsystem — exists in service of that positioning. AI support is not a downstream
consumer of PCB-IR bolted on after the fact; it is one of the reasons the IR is shaped the way
it is.
