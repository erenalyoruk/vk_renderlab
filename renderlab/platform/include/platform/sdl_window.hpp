#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include "base/noncopyable.hpp"

namespace rl::platform {
struct window_config {
  std::string title = "Vulkan RenderLab";
  std::int32_t width = 1280;
  std::int32_t height = 720;
};

class sdl_window final : public noncopyable {
 public:
  explicit sdl_window(const window_config& config);
  ~sdl_window() noexcept;

  [[nodiscard]] bool poll_events() const;
  [[nodiscard]] SDL_Window* native_handle() const noexcept;
  [[nodiscard]] static std::vector<const char*> required_vulkan_extensions();
  [[nodiscard]] VkSurfaceKHR create_vulkan_surface(VkInstance instance) const;

 private:
  SDL_Window* window_ = nullptr;
  bool sdl_initialized_ = false;
};
}  // namespace rl::platform
