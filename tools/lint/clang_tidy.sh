#!/usr/bin/env bash
set -euo pipefail

cd "${BUILD_WORKSPACE_DIRECTORY:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"

refresh_compile_commands=0

if [[ "${1:-}" == "--refresh" ]]; then
  refresh_compile_commands=1
  shift
fi

target="${1:-//apps/renderlab:renderlab}"

if [[ "${refresh_compile_commands}" -eq 1 || ! -f compile_commands.json ]]; then
  echo "generating compile_commands.json for ${target}"
  bazel run //:compile_commands -- "${target}"
else
  echo "using existing compile_commands.json; pass --refresh to regenerate it"
fi

filtered_compile_commands_dir="$(mktemp -d)"
filtered_files="${filtered_compile_commands_dir}/files.txt"
trap 'rm -rf "${filtered_compile_commands_dir}"' EXIT

python3 - "${filtered_compile_commands_dir}/compile_commands.json" "${filtered_files}" <<'PY'
import json
import pathlib
import sys

output_path = pathlib.Path(sys.argv[1])
files_path = pathlib.Path(sys.argv[2])
workspace = pathlib.Path.cwd()
allowed_prefixes = ("apps/", "renderlab/")
source_suffixes = (".c", ".cc", ".cpp", ".cxx")

with pathlib.Path("compile_commands.json").open(encoding="utf-8") as compile_commands_file:
    compile_commands = json.load(compile_commands_file)

filtered = []
files = []
seen_files = set()

for entry in compile_commands:
    file_name = pathlib.PurePosixPath(str(entry.get("file", "")).replace("\\", "/"))

    if file_name.is_absolute():
        try:
            file_name = pathlib.PurePosixPath(pathlib.Path(file_name).relative_to(workspace).as_posix())
        except ValueError:
            continue

    normalized_file_name = file_name.as_posix()

    if not normalized_file_name.startswith(allowed_prefixes):
        continue

    if not normalized_file_name.endswith(source_suffixes):
        continue

    filtered.append(entry)

    if normalized_file_name not in seen_files:
        files.append(normalized_file_name)
        seen_files.add(normalized_file_name)

if not filtered:
    raise SystemExit("compile_commands.json has no entries for apps/ or renderlab/ source files")

output_path.parent.mkdir(parents=True, exist_ok=True)

with output_path.open("w", encoding="utf-8") as output_file:
    json.dump(filtered, output_file, indent=2)
    output_file.write("\n")

with files_path.open("w", encoding="utf-8") as output_file:
    for file_name in files:
        output_file.write(file_name)
        output_file.write("\n")

print(f"filtered compile commands: {len(filtered)} project source entr{'y' if len(filtered) == 1 else 'ies'}")
PY

mapfile -t files < "${filtered_files}"

clang-tidy \
  --quiet \
  -p "${filtered_compile_commands_dir}" \
  --config-file=.clang-tidy \
  --header-filter='^(.*/)?(apps|renderlab)/' \
  --exclude-header-filter='(^|.*/)(external|third_party|bazel-[^/]*)(/|$)' \
  "${files[@]}"
