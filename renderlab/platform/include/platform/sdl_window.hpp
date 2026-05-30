#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include "base/noncopyable.hpp"

namespace rl::platform {

class sdl_video_session final : public noncopyable {
 public:
  sdl_video_session();
  ~sdl_video_session() noexcept;
};

struct sdl_window_deleter {
  void operator()(SDL_Window* window) const noexcept;
};

struct window_config {
  std::string title = "Vulkan RenderLab";
  std::int32_t width = 1280;
  std::int32_t height = 720;
};

class sdl_window final : public noncopyable {
 public:
  explicit sdl_window(const window_config& config);
  ~sdl_window() noexcept = default;

  [[nodiscard]] bool poll_events() const;
  [[nodiscard]] SDL_Window* native_handle() const noexcept;
  [[nodiscard]] static std::vector<std::string> required_vulkan_extensions();
  [[nodiscard]] VkSurfaceKHR create_vulkan_surface(VkInstance instance) const;

 private:
  sdl_video_session video_session_;
  std::unique_ptr<SDL_Window, sdl_window_deleter> window_;
};

}  // namespace rl::platform
