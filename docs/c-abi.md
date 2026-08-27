# PCB-IR C ABI

The C ABI (`include/pcbir/pcbir.h`) is the primary linkable integration surface, designed so
closed-source EDA tools on arbitrary toolchains can integrate reliably. C++20 is an internal
implementation detail behind it. Full design rationale and alternatives considered live in
`docs/rfcs/0001-stable-c-abi.md`; this document is the durable reference for what the ABI
actually is and how to extend it safely.

## Conventions

- **Opaque handles only.** Every stateful object (a loaded board, a per-layer snapshot, a
  workspace, a diagnostics list) is a forward-declared, incomplete `typedef struct` — a C
  consumer never sees the real layout, which lives in a `.cpp` file under `src/pcbir/c_abi/`.
- **Per-entity-typed handle structs**, not one generic handle or a raw pointer. An entity
  reference is a small `{uint32_t index; uint32_t generation;}` value (mirroring
  `core::Handle<Tag>`), with a distinct C type per entity kind (`pcbir_via_handle_t`,
  `pcbir_net_handle_t`, ...) so two different entity kinds' handles are never
  interchangeable, even though their layout is identical. A handle is meaningful only
  relative to the `pcbir_*_snapshot_t` it was read from. Every entity also has a durable
  `uint64_t` EntityId, retrievable via a `..._entity_id()` accessor and a
  `..._find_by_entity_id()` lookup, since a handle does not survive a save/reload.
- **Snapshots are independently owned once obtained.** `pcbir_board_geometry()` /
  `_connectivity()` / `_stackup()` hand back a copy (cheap — the underlying `Snapshot` is
  copy-on-write) with its own `_free`; it stays valid after the `pcbir_board_t` it came from
  is freed. A `const char*` a getter returns is valid for exactly the lifetime of the snapshot
  handle it came from and is never invalidated by a later workspace commit. Snapshot handles
  are safe to read concurrently from multiple threads with no external synchronization.
- **Workspace handles expose insert/commit/free only, no reads**, and are not thread-safe —
  mutate one from a single thread at a time, matching the underlying `Workspace`.
- **No exceptions cross the boundary.** Every entry point returns a `pcbir_status_t`;
  `pcbir_last_error()` (thread-local, valid until the next `pcbir_*` call, matching
  errno/strerror) gives a human-readable detail string for the last non-OK call.
- **Fixed-width enums.** Every enum-like C type is a fixed-width integer typedef plus an
  unscoped `enum{}` (`typedef int32_t pcbir_status_t; enum { PCBIR_OK = 0, ... };`), never a
  bare C `enum` used directly as a parameter/return type — C leaves enum underlying-type width
  implementation-defined, and the project's compiler matrix (GCC 11+/Clang 14+/MSVC 2022+)
  cannot be assumed to agree.
- **Ownership of allocated buffers.** Every `_load`/`_create`/`_commit`/`_validate` that hands
  back a new handle has a matching `_free`. `pcbir_board_save_bytes()`'s raw byte buffer is
  freed with `pcbir_bytes_free()` — the one deliberate exception to "opaque getters only",
  since a byte buffer has no fields that could ever grow.

## Stability policy

- Internal C++ refactors MUST NOT change the C ABI. The C-ABI smoke test
  (`tests/c_abi/smoke_test.c`, compiled and linked as genuine C11) guards this, alongside a
  standalone `pcbir.h`-is-valid-C syntax check, both wired into `ctest`.
- Never change the layout of a published struct (the five `{index, generation}` handle types,
  or the fixed-width status/diagnostic-code typedefs) — add new versioned entry points
  instead.
- `PCBIR_ABI_VERSION` (`pcbir.h`) is the ABI's own compatibility class, deliberately
  independent of `PCBIR_VERSION_MAJOR` (`version.h`, the library's semver) — it is bumped only
  for a breaking ABI change, never reset by an unrelated library version bump.
- Symbol visibility is wired via CMake's `GenerateExportHeader` (`pcbir_c_abi` target,
  `src/CMakeLists.txt`) even though the library ships static-only today — hidden visibility by
  default, so a future shared build never needs retrofitting. Static-vs-shared shipping and
  any SONAME scheme are deferred to a future packaging RFC.
- Any ABI change requires explicit maintainer approval with written justification before
  implementation (`AGENTS.md` → Commit & Change-Control), formalized as a new
  `docs/rfcs/NNNN-*.md` per `docs/rfcs/README.md`.

## Surface (current)

Whole-board load/save (file and in-memory bytes), per-layer snapshot access, read-only
accessors for five entities chosen to exercise every field shape the model has —
`Via` (geometry), `Net` and `Pin` (connectivity), `Material` and `Layer` (stackup) —
diagnostics retrieval for all three layers, and one workspace edit/commit loop (`Net` insert
on connectivity) closed by `pcbir_board_with_connectivity` so an edit round-trips back into a
saveable board.

| Area | Functions |
|---|---|
| Errors | `pcbir_last_error` |
| Board | `pcbir_board_load_file`, `pcbir_board_load_bytes`, `pcbir_board_save_file`, `pcbir_board_save_bytes`, `pcbir_bytes_free`, `pcbir_board_free`, `pcbir_board_format_version`, `pcbir_board_geometry`, `pcbir_board_connectivity`, `pcbir_board_stackup`, `pcbir_board_with_connectivity` |
| Geometry: Via | `pcbir_geometry_via_count`, `_at`, `_find_by_entity_id`, `_entity_id`, `_position`, `_drill_diameter_nm`, `_finished_hole_diameter_nm`, `_pad_diameter_nm`, `_annular_ring_nm`, `_start_layer_entity_id`, `_end_layer_entity_id`, `pcbir_geometry_snapshot_free` |
| Geometry diagnostics | `pcbir_geometry_validate`, `pcbir_geometry_diagnostics_free`, `_count`, `_at` |
| Connectivity: Net | `pcbir_connectivity_net_count`, `_at`, `_find_by_entity_id`, `_entity_id`, `_name`, `pcbir_connectivity_snapshot_free` |
| Connectivity: Pin | `pcbir_connectivity_pin_count`, `_at`, `_find_by_entity_id`, `_entity_id`, `_pad_entity_id`, `_net_entity_id` |
| Connectivity diagnostics | `pcbir_connectivity_validate`, `pcbir_connectivity_diagnostics_free`, `_count`, `_at` |
| Connectivity workspace | `pcbir_connectivity_workspace_create`, `_create_from_snapshot`, `_free`, `_insert_net`, `_commit` |
| Stackup: Material | `pcbir_stackup_material_count`, `_at`, `_find_by_entity_id`, `_entity_id`, `_name`, `_dielectric_constant_e6`, `_loss_tangent_e6`, `pcbir_stackup_snapshot_free` |
| Stackup: Layer | `pcbir_stackup_layer_count`, `_at`, `_find_by_entity_id`, `_entity_id`, `_name`, `_kind`, `_thickness_nm`, `_roughness_nm`, `_material_entity_id` |
| Stackup diagnostics | `pcbir_stackup_validate`, `pcbir_stackup_diagnostics_free`, `_count`, `_at` |

Explicitly deferred as additive follow-up (see `docs/rfcs/0001-stable-c-abi.md` → Open
questions): the nested `Polygon`/`Contour`/`Span`/`Segment`/`Arc` geometry accessors (and
therefore `Pad`, `CopperPour`, `Keepout`, `MaskOpening`, `Track`, `SilkscreenGraphic`,
`DrillHit`'s full field set), workspace insert/edit for geometry and stackup,
`Extension`/`PassthroughBlob` C accessors, `Bus`/`DifferentialPair` accessors, and
`pcbir::BoardFileView`'s zero-copy mmap path.
