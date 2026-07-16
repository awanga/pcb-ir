# PCB-IR C++ Style Guide

> Status: stub.

This project targets **C++20**. The conventions below apply to all C++ sources in this
repository.

## Baseline

- **Formatting:** `clang-format` with an LLVM base style (`.clang-format` at repo root).
- **Linting:** `clang-tidy` (`.clang-tidy` at repo root). Zero warnings before commit.
- **Standard:** C++20.

## Conventions

- RAII required. Exceptions allowed only at API boundaries (never across the C ABI).
- Prefer composition over inheritance; no deep inheritance trees; avoid RTTI-heavy designs.
- Use spans/views; minimize copies and heap fragmentation; favor data-oriented, cache-
  friendly layouts.
- No hidden mutable global state.

## Build flags

`-std=c++20 -Wall -Wextra -Werror` (plus project warnings). Format + lint are CI gates.

(To be expanded with naming conventions and examples.)
