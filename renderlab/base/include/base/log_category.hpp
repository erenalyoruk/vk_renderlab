#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rl::log {
enum class log_category : std::uint8_t {
  core,
  app,
  platform,
  vulkan,
  gpu,
  render,
  shader,
  assets,
  ui,
  profiling,
  count,
};

inline constexpr std::size_t category_count = static_cast<std::size_t>(log_category::count);

[[nodiscard]] constexpr std::size_t category_index(log_category category) noexcept {
  return static_cast<std::size_t>(category);
}

[[nodiscard]] constexpr std::string_view to_string(log_category category) noexcept {
  switch (category) {
    case log_category::core:
      return "core";
    case log_category::app:
      return "app";
    case log_category::platform:
      return "platform";
    case log_category::vulkan:
      return "vulkan";
    case log_category::gpu:
      return "gpu";
    case log_category::render:
      return "render";
    case log_category::shader:
      return "shader";
    case log_category::assets:
      return "assets";
    case log_category::ui:
      return "ui";
    case log_category::profiling:
      return "profiling";
    case log_category::count:
      return "unknown";
  }

  return "unknown";
}
}  // namespace rl::log
