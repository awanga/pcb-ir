# PCB-IR RFCs

> Status: stub — process defined now; the first RFC lands with the first change that needs
> one.

This directory formalizes `AGENTS.md`'s change-control rule — *"Any major architectural
change or any change to the serialized schema or the C ABI must be elevated to the user [or,
for external contributions, the maintainers] for explicit approval, with written
justification, BEFORE implementation"* — into a written, reviewable proposal process, the way
most long-lived open-source infrastructure projects (LLVM, Rust, Python) handle changes big
enough that a PR diff alone can't carry the discussion.

## When an RFC is required

- Any change to the serialized **schema** (`schemas/*.fbs`) beyond an additive, backward
  compatible field.
- Any change to the **C ABI** (`include/pcbir/pcbir.h`).
- Any change to an **MCP service contract** (`docs/mcp/`) — new services are lower-friction
  than changing an existing operation's signature.
- Any new **semantic layer**, or a change to the boundary between existing layers
  (`docs/architecture.md` → "Semantic layers").
- Adding a new **required** dependency to the default (lean-core) build.
- Any change significant enough that a reviewer would reasonably ask "why wasn't this
  discussed before it was implemented?"

Bug fixes, additive extensions (`docs/extensions-governance.md`), new importers/exporters
that don't change core schema, and ordinary feature work within an already-agreed `TASKS.md`
phase do **not** need an RFC.

## Process

1. Copy `0000-template.md` to `NNNN-short-title.md` (next available number).
2. Fill in: motivation, proposed design, alternatives considered, backward-compatibility
   impact, and translation-fidelity impact if the change touches import/export.
3. Open it for review before implementation begins — not alongside or after.
4. Once accepted, implementation follows the normal commit gates in `AGENTS.md`.
5. An accepted RFC is retained even after its change ships, as the historical record of *why*
   — the same provenance discipline `docs/architecture.md`'s Layer 6 (Intent & Planning)
   applies to a single board's design history, applied here to the project's own design
   history.

## Template

See `0000-template.md`.
