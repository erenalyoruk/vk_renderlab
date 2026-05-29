# Vulkan RenderLab

Small Vulkan renderer lab in modern C++23.

## Build

Requirements on Linux:

- Bazelisk or Bazel 9.1.0
- clang / clang++
- CMake and Ninja for SDL3 through `rules_foreign_cc`
- Vulkan-capable GPU/driver

```bash
bazel build //apps/renderlab:renderlab --config=dev
bazel run //apps/renderlab:renderlab --config=dev
```

Build shaders only:

```bash
bazel build //renderlab/shaders:all_shaders --config=dev
./tools/lint/slang_check.sh
```

Inspect the Slang compiler exposed by the Vulkan SDK repository:

```bash
bazel run @vk_sdk//:slangc -- -h
```

Generate `compile_commands.json` for clang/clang-tidy:

```bash
bazel run //:compile_commands -- //apps/renderlab:renderlab
```

Run formatting and static-analysis helpers:

```bash
./tools/lint/clang_format.sh
./tools/lint/clang_tidy.sh //apps/renderlab:renderlab
bazel run //:buildifier.check
```
