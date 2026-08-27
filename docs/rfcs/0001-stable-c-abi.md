# RFC 0001: Stable C ABI (`include/pcbir/pcbir.h`)

- **Status:** Accepted
- **Author(s):** Claude Sonnet 5 (agentic), on behalf of Alfred Wanga
- **Date:** 2026-08-27

## Motivation

The C ABI is PCB-IR's primary linkable integration surface — the mechanism closed-source EDA
tools on arbitrary toolchains use to link against PCB-IR reliably, with the same standing as
the versioned file format itself (`docs/c-abi.md`). Every layer built so far (geometry,
connectivity, stackup, board composition) is pure internal C++; this is the first change to
`include/pcbir/pcbir.h` itself, which `AGENTS.md` and this directory's own process both
require to be elevated for explicit approval, with written justification, before
implementation — because once published, a C ABI can never break ("never change the layout of
a published struct — add new versioned entry points instead").

## Design

### Conventions

- **Entity references are per-entity-typed `{index, generation}` POD structs**
  (`pcbir_via_handle_t`, `pcbir_net_handle_t`, `pcbir_pin_handle_t`, `pcbir_material_handle_t`,
  `pcbir_layer_handle_t`), mirroring `core::Handle<Tag>` exactly — never one generic struct
  reused across entity kinds, and never a raw pointer (`Arena::erase()` destroys the `T` in
  place; every accessor re-resolves the handle fresh on each call, so a stale handle degrades
  to `PCBIR_ERROR_NOT_FOUND` instead of dangling). A handle is valid only relative to the
  snapshot it came from — inherited from `Handle<Tag>` today, documented rather than "fixed".
  Every entity also exposes a durable `uint64_t` `EntityId` accessor plus a `find_by_entity_id`
  lookup, since a handle doesn't survive a reload.
- **Per-layer snapshots are independently-owned copies, not views tied to a parent's
  lifetime.** `pcbir_board_geometry()`/`_connectivity()`/`_stackup()` hand back a
  `Snapshot<Components...>` copy (O(1), copy-on-write-shared backing) with its own `_free`,
  valid after the parent `pcbir_board_t` is freed.
- **Field accessors only ever take a `pcbir_*_snapshot_t`, never a live workspace.** A
  `Snapshot` is genuinely immutable for its own lifetime, so the string-pointer rule is simple
  and permanent: a `const char*` returned by any getter is valid for exactly the lifetime of
  the snapshot handle it came from, never invalidated by a later workspace commit. Workspace
  handles expose insert/commit/free only, no reads. The one deliberate exception is
  `pcbir_last_error()`: thread-local, valid-until-next-call, matching errno/strerror.
- **No exceptions cross the boundary**, via one shared internal helper
  (`translate_exceptions<Fn>()`, `src/pcbir/c_abi/error.hpp`, not a public header) wrapping
  every `extern "C"` body in the same chain: `pcbir::FormatError` → `std::exception` → bare
  `catch (...)` → success. Every pointer/index argument is also null/range-checked explicitly
  before use — try/catch does not intercept undefined behavior from a bad argument, only
  thrown exceptions.
- `pcbir::FormatError` gains a small **additive** enhancement so
  `PCBIR_ERROR_CORRUPT_BUFFER`/`PCBIR_ERROR_UNSUPPORTED_VERSION` are genuinely distinguishable:
  a `Reason` enum (`CorruptBuffer`/`UnsupportedVersion`), a `reason()` accessor, and a second
  constructor taking `(Reason, message)`. The existing single-string constructor is unchanged
  and keeps defaulting to `CorruptBuffer`, matching every existing call site's actual intent.
  This is non-breaking C++, not itself a schema or ABI change.
- **Every enum-like C type is a fixed-width integer typedef plus an unscoped `enum{}`**
  (`typedef int32_t pcbir_status_t; enum { PCBIR_OK = 0, ... };`), never a bare C `enum` used
  directly as a parameter/return type — C leaves enum underlying-type width
  implementation-defined, and the project's compiler matrix (GCC 11+/Clang 14+/MSVC 2022+)
  cannot be assumed to agree. Booleans are `int` (0/1), not `<stdbool.h>`.
- **Diagnostics lists are three distinct opaque types**
  (`pcbir_geometry_diagnostics_t`/`_connectivity_diagnostics_t`/`_stackup_diagnostics_t`), for
  the same type-safety reasoning as entity handles.
- `pcbir_board_save_bytes` returns a raw `(uint8_t**, size_t*)` pair plus a matching
  `pcbir_bytes_free` — a deliberate, narrow exception to "opaque getters only", since a byte
  buffer has no fields that could ever grow.
- **Symbol visibility and a distinct ABI version number are wired now**, even though this
  ships static-only: a dedicated `pcbir_c_abi` CMake target (depends `PRIVATE` on
  `pcbir_core`, publicly exposes only `pcbir.h`), built via `GenerateExportHeader` for the
  `PCBIR_API` macro, hidden visibility by default. `PCBIR_ABI_VERSION` in the header is a
  separate number from `PCBIR_VERSION_MAJOR` (`version.h`) — the ABI's compatibility class
  must not reset just because the library's own semver is pre-1.0. Retrofitting visibility
  control after real consumers exist would itself be a breaking change to the deployment
  contract.
- Documented, not new code: **snapshot handles are safe to read concurrently across
  threads** (immutable, copy-on-write-shared via atomic refcounting); **workspace handles are
  not** (mutate from one thread at a time, matching the underlying `Workspace`).

### Scope

In scope: whole-board load/save (file and in-memory bytes), per-layer snapshot access,
read-only accessors for five representative entities chosen to exercise every field shape the
model has — `Via` (geometry: scalars + two cross-layer `EntityId` references + a derived
getter), `Net` and `Pin` (connectivity: a string; a cross-reference pair), `Material` and
`Layer` (stackup: string + fixed-point scalars; string + enum + scalars + an `EntityId`
reference) — diagnostics retrieval for all three layers, and one full workspace edit/commit
loop (`Net` insert on connectivity, chosen as the simplest entity) closed by
`pcbir_board_with_connectivity` so an edit can round-trip back into a saveable board.

Explicitly deferred as additive follow-up, not because it is difficult but to keep this RFC
reviewable and prove the pattern before committing to full coverage forever: the nested
`Polygon`/`Contour`/`Span`/`Segment`/`Arc` geometry accessors (and therefore `Pad`,
`CopperPour`, `Keepout`, `MaskOpening`, `Track`, `SilkscreenGraphic`, `DrillHit`'s full field
set), workspace insert/edit for geometry and stackup, `Extension`/`PassthroughBlob` C
accessors, `Bus`/`DifferentialPair` accessors, and `pcbir::BoardFileView`'s zero-copy mmap
path.

### Header

The full text of `include/pcbir/pcbir.h` as implemented is the normative design; see that
file. It defines `pcbir_status_t` and eight status codes; `pcbir_last_error()`; opaque types
for a board, three per-layer snapshots, one workspace, and three diagnostics lists; five
per-entity-typed handle structs; `pcbir_layer_kind_t` and three per-layer diagnostic-code
typedefs; and ~62 functions named `pcbir_<layer>_<entity>_<field>`, mirroring
`pcbir::<layer>::<Entity>::<field>` exactly.

### Implementation layout

- `src/pcbir/c_abi/error.hpp`/`.cpp` — thread-local last-error storage and the shared
  exception-translation helper.
- `src/pcbir/c_abi/board.cpp`, `geometry.cpp`, `connectivity.cpp`, `stackup.cpp` — one file per
  layer, each defining its opaque types' real struct bodies (a thin wrapper around the
  corresponding C++ snapshot/workspace type) and its `extern "C"` functions, plus a
  `static_assert` per diagnostic-code enum checking numeric parity against the C++
  `DiagnosticCode` enum, so an appended C++ code that isn't mirrored into the header is a
  build break, not silent drift.
- `tools/c_abi/generate_fixture.cpp` — builds a small sample board in memory (the same
  `Workspace::insert(...)` pattern the existing C++ tests already use) and serializes it to a
  path given on argv, run at build/test time into the build tree. Never a committed binary
  (`AGENTS.md`: "Do not commit: generated binaries").
- `tests/c_abi/smoke_test.c` — genuine C11, compiled and linked against only `pcbir_c_abi`,
  proving every scoped-in surface bullet end-to-end: load the fixture, read a `Via`, a
  `Material`, a `Pin`; run geometry diagnostics; edit connectivity (insert a `Net`, commit,
  read it back) and re-attach it to the board.

## Alternatives considered

- **A single generic `{index, generation}` handle struct reused for every entity kind** —
  rejected: it throws away the compile-time protection `Handle<Tag>` already provides in C++;
  a `Net` handle and a `Material` handle become structurally identical and silently swappable,
  resolving to the wrong entity instead of erroring.
- **Views tied to the parent board's lifetime** (the sqlite3_stmt/libgit2 pattern) for
  per-layer snapshots — rejected in favor of independently-owned copy-on-write copies, which
  the existing `Snapshot` type already makes cheap; avoids an entire class of lifetime bugs.
- **`_string_free`-based string ownership** — rejected in favor of a lifetime-bound-to-snapshot
  rule, since `Snapshot` is already immutable; halves the surface (no read accessors on
  workspaces at all) and avoids ever reasoning about a workspace reallocating storage a caller
  still holds a pointer into.
- **A bare C `enum` for status/diagnostic codes** — rejected: C leaves the underlying type's
  width implementation-defined, a real risk across the project's GCC/Clang/MSVC matrix; fixed
  -width typedef plus unscoped `enum{}` instead.
- **Full nested geometry and all sixteen entity types in this RFC** — rejected as unreviewable
  in one document; scoped to five representative entities proving every field shape, with full
  coverage as clearly-labeled additive follow-up.
- **One generic diagnostics-list type plus a layer discriminant** — rejected for the same
  type-safety reasoning as the handle decision above.
- **An opaque `pcbir_byte_buffer_t` for `pcbir_board_save_bytes`** — rejected as unnecessary
  ceremony; a byte buffer has no fields that could ever grow, so there is nothing
  struct-versioning needs to protect.

## Backward compatibility

- **Schema:** unaffected — no `.fbs` change.
- **C ABI:** this RFC is the initial publication; there is nothing to break yet. Every future
  extension must be additive per `docs/c-abi.md`'s stability policy (new versioned entry
  points; never repurpose a field in a published struct). The only "structs" this RFC freezes
  are the five `{index, generation}` handle types and the fixed-width status/diagnostic-code
  typedefs.
- **MCP service contracts:** unaffected.

## Translation-fidelity impact

None — this is a read/edit/diagnostics API surface over the existing in-memory model, not an
importer or exporter.

## Open questions

- Static-vs-shared `pcbir_c_abi` shipping, and any SONAME/versioning scheme, is deliberately
  punted to a future packaging RFC; only the visibility/export-macro plumbing is decided now.
- Whether `BoardFileView`'s zero-copy mmap path gets its own C accessors, or stays a
  C++-only optimization, is open until a concrete consumer needs it from C.
- The full nested-geometry accessor surface (`Polygon`/`Contour`/`Span`/`Segment`/`Arc`) will
  need its own design pass when the remaining eleven entity types are added — the flat
  `pcbir_<layer>_<entity>_<field>` pattern established here does not obviously extend to a
  recursive shape without more thought (e.g. whether a `Contour`'s spans are walked via an
  index-based accessor mirroring `Arena`, or flattened into a caller-supplied buffer).
