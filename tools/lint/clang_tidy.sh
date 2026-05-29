#!/usr/bin/env bash
set -euo pipefail

cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

target="${1:-//apps/renderlab:renderlab}"

echo "generating compile_commands.json for ${target}"
bazel run //:compile_commands -- "${target}"

mapfile -t files < <(find apps renderlab -type f \( \
  -name '*.cpp' -o \
  -name '*.cc' -o \
  -name '*.cxx' \
  \) | sort)

if [[ "${#files[@]}" -eq 0 ]]; then
  echo "no C++ source files found"
  exit 0
fi

clang-tidy \
  -p . \
  --config-file=.clang-tidy \
  "${files[@]}"
