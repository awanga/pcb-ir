#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Public-header hygiene gate: include/pcbir/ must never name a third-party
# type (FlatBuffers, Clipper2, Boost) in a signature -- those stay wrapped
# behind PCB-IR's own types/handles, so swapping a dependency is never a
# public API break (docs/architecture.md).
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
include_dir="${root_dir}/include/pcbir"
pattern='(flatbuffers|Clipper2Lib|boost)::|#[[:space:]]*include[[:space:]]*[<"](flatbuffers|clipper2|boost)'

# `mapfile`/`readarray` need bash >= 4; macOS ships bash 3.2 as /bin/bash, so
# this counts/searches via find+xargs instead of reading into an array.
header_count="$(find "${include_dir}" -type f \( -name '*.h' -o -name '*.hpp' \) | wc -l | tr -d ' ')"

if [ "${header_count}" -eq 0 ]; then
  echo "check_public_header_hygiene: no headers found under ${include_dir}" >&2
  exit 1
fi

matches="$(find "${include_dir}" -type f \( -name '*.h' -o -name '*.hpp' \) -print0 \
  | xargs -0 grep -nE "${pattern}" -- || true)"

if [ -n "${matches}" ]; then
  echo "public header hygiene violation(s):" >&2
  echo "${matches}" >&2
  exit 1
fi

echo "public header hygiene: OK (${header_count} headers checked)"
