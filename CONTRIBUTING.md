# Contributing to PCB-IR

## Build prerequisites

- CMake >= 3.21
- A C++20 compiler (GCC 11+, Clang 14+, or MSVC 2022+)
- Python 3.8+ (used only to install Conan and the clang tools below)

Conan, `clang-format`, `clang-tidy`, and `ninja` are consumed as pinned pip packages rather than
system packages, so every contributor and CI runner uses the exact same tool versions regardless
of what their OS package manager ships:

```sh
python3 -m venv .venv
source .venv/bin/activate        # .venv\Scripts\activate on Windows
pip install conan==2.30.0 clang-format==22.1.8 clang-tidy==22.1.8 ninja
```

(If your Python install is externally managed and `venv` creation fails with an `ensurepip`
error, use `python3 -m venv --without-pip --system-site-packages .venv` and then
`python3 -m pip install ...` inside it.)

## Building

```sh
source .venv/bin/activate
conan profile detect --force        # first time only
conan install . -s compiler.cppstd=20 -c tools.cmake.cmaketoolchain:generator=Ninja --lockfile=conan.lock --build=missing
cmake --preset conan-release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build --preset conan-release
ctest --test-dir build/Release --output-on-failure
```

Optional components are gated behind CMake flags (all default `OFF`, see `CMakeLists.txt`):
`PCBIR_BUILD_PYTHON`, `PCBIR_BUILD_VIZ`, `PCBIR_BUILD_BENCH`, `PCBIR_BUILD_FUZZ`. Enabling one
also requires the matching Conan option (e.g. `-o with_bench=True`), and regenerating
`conan.lock` for that option combination (`conan.lock` only pins the default lean-core set).

## Before committing

All of the following must pass before committing non-trivial changes (enforced in CI):

```sh
clang-format --dry-run -Werror $(git ls-files '*.c' '*.cc' '*.cpp' '*.h' '*.hpp')
clang-tidy -p build/Release $(git ls-files '*.cpp' '*.cc')
cmake --build --preset conan-release
ctest --test-dir build/Release --output-on-failure
```

## Style

C++ sources follow `docs/cpp-style.md` (LLVM-based, enforced by `.clang-format`/`.clang-tidy`).

## Branching & commits

- `feature/`, `bugfix/` branch off `develop`; rebase onto `develop` before merging; delete
  after merge. `experimental/` also branches off `develop` and may stay open indefinitely.
- Only `release/` branches merge into `main`.
- Commits are singular in purpose, imperative subject line, `<=72` chars; explain *why* in the
  body when it isn't obvious from the diff.

## Schema / ABI / architecture changes

Any change to the serialized schema, the C ABI (`include/pcbir/pcbir.h`), or a major
architectural decision requires explicit maintainer approval with written justification
*before* implementation.

## Documentation

```sh
pip install mkdocs mkdocs-material
mkdocs build
```
