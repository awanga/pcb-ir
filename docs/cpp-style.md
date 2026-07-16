# PCB-IR C++ Style Guide

This project targets **C++20**. The conventions below apply to all C++ sources in this
repository.

## Baseline

- **Formatting:** `clang-format` with an LLVM base style (`.clang-format` at repo root),
  2-space indent, 100-column limit, left-aligned pointers/references.
- **Linting:** `clang-tidy` (`.clang-tidy` at repo root). Zero warnings before commit; the
  config treats every enabled check as an error (`WarningsAsErrors: '*'`).
- **Standard:** C++20.

## Conventions

- RAII required. Exceptions allowed only at API boundaries (never across the C ABI).
- Prefer composition over inheritance; no deep inheritance trees; avoid RTTI-heavy designs.
- Use spans/views; minimize copies and heap fragmentation; favor data-oriented, cache-
  friendly layouts.
- No hidden mutable global state.

## Naming conventions

Enforced by `readability-identifier-naming` in `.clang-tidy`.

| Entity | Convention | Example |
|---|---|---|
| Namespace | `lower_case` | `namespace pcbir::geometry` |
| Class / struct | `CamelCase` | `class SnapshotReader;` |
| Function / method | `lower_case` | `size_t entity_count() const;` |
| Local / member variable | `lower_case` | `size_t layer_count;` |
| Private member | `lower_case` + trailing `_` | `Arena* arena_;` |
| Enum / enum class | `CamelCase` | `enum class LayerKind { ... };` |
| Enum constant | `CamelCase` | `LayerKind::Copper` |
| Macro | `UPPER_CASE` | `PCBIR_VERSION_MAJOR` |
| Global constant | `UPPER_CASE` | `constexpr int64_t MAX_COORD;` |

Header guards use `#ifndef`/`#define` (not `#pragma once`) so public headers stay valid when
included from strict C ABI consumers; the macro name mirrors the path, e.g.
`PCBIR_VERSION_H` for `include/pcbir/version.h`.

## File layout

- Public headers: `include/pcbir/<area>/<name>.h` (or `.hpp` for C++-only headers that are
  never included from the C ABI). C ABI headers must compile as C (see `docs/c-abi.md`).
- Implementation: `src/pcbir/<area>/<name>.cpp`, mirroring the public header path.
- One primary class/abstraction per file; free functions grouped by area, not by file.

## Example

```cpp
// include/pcbir/geometry/point.hpp
#ifndef PCBIR_GEOMETRY_POINT_HPP
#define PCBIR_GEOMETRY_POINT_HPP

#include <cstdint>

namespace pcbir::geometry {

// Coordinates are signed 64-bit nanometers (see docs/format-spec.md).
struct Point {
  int64_t x = 0;
  int64_t y = 0;
};

[[nodiscard]] Point translate(const Point& p, const Point& delta);

}  // namespace pcbir::geometry

#endif  // PCBIR_GEOMETRY_POINT_HPP
```

## Build flags

`-std=c++20 -Wall -Wextra -Werror` (plus project warnings). Format + lint are CI gates.
