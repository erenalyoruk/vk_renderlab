#!/usr/bin/env bash
set -euo pipefail

cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

bazel build //renderlab/shaders:all_shaders --config=dev
