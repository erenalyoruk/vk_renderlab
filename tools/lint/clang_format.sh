#!/usr/bin/env bash
set -euo pipefail

cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

mapfile -t files < <(find apps renderlab -type f \( \
  -name '*.cpp' -o \
  -name '*.cc' -o \
  -name '*.cxx' -o \
  -name '*.h' -o \
  -name '*.hpp' \
  \) | sort)

if [[ "${#files[@]}" -eq 0 ]]; then
  echo "no C++ files found"
  exit 0
fi

clang-format -i "${files[@]}"
