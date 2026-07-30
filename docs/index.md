# PCB-IR

An open, extensible intermediate representation framework for PCB physical design and EDA
interoperability — see the project `README.md` for the full vision.

## Documentation

- [Architecture](architecture.md) — snapshots, workspaces, memory model, layering, the intent
  compilation pipeline, and the MCP subsystem.
- [File format specification](format-spec.md) — the on-disk schema and versioning policy.
- [C ABI](c-abi.md) — the stable integration surface for non-C++ toolchains.
- [MCP subsystem](mcp/index.md) — the stable integration surface for AI systems: service
  taxonomy, philosophy, capability negotiation.
- [Conformance](conformance.md) — the validator, golden-file corpus, and translation-fidelity
  reporting.
- [Extensions & governance](extensions-governance.md) — namespacing and capability negotiation.
- [RFCs](rfcs/README.md) — the proposal process for major/breaking changes.
- [Glossary](glossary.md) — canonical terms, including the Intent & Planning layer.
- [C++ style guide](cpp-style.md) — internal coding conventions.

Most of these are stubs, filled in as development progresses.
