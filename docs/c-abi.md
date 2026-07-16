# PCB-IR C ABI

> Status: stub.

The C ABI (`include/pcbir/pcbir.h`) is the primary linkable integration surface, designed so
closed-source EDA tools on arbitrary toolchains can integrate reliably. C++20 is an internal
implementation detail.

## Rules

- Opaque handles only; no C++ types, templates, or STL in any signature.
- The header must compile as C (not just C++).
- No exceptions cross the boundary. Every entry point returns a status code; a
  thread-local last-error message is queryable.
- Versioned struct sizes; never change the layout of a published struct — add new versioned
  entry points instead.

## Stability policy

- Internal C++ refactors MUST NOT change the C ABI. The C-ABI smoke test (compiled and linked
  as C) guards this in CI.
- Any ABI change is user-gated with written justification.

## Surface (MVP)

- Load / save (file format).
- Snapshot read (entities, nets, layers, geometry).
- Workspace edit / commit.
- Diagnostics retrieval.

(To be expanded with the actual function list.)
