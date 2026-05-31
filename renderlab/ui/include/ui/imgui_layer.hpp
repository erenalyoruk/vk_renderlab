#pragma once

#include <cstdint>

#include <SDL3/SDL_events.h>
#include <vulkan/vulkan.h>

#include "base/noncopyable.hpp"

namespace rl::platform {
class sdl_window;
}

namespace rl::vulkan {
class vulkan_context;
struct renderer_settings;
struct renderer_status;
}  // namespace rl::vulkan

namespace rl::ui {

struct imgui_render_target {
  VkFormat color_format = VK_FORMAT_UNDEFINED;
  std::uint32_t min_image_count = 2;
  std::uint32_t image_count = 2;
  std::uint64_t generation = 0;
};

class imgui_layer final : public noncopyable {
 public:
  imgui_layer(rl::platform::sdl_window& window, const rl::vulkan::vulkan_context& context,
              imgui_render_target render_target);
  ~imgui_layer() noexcept;

  imgui_layer(imgui_layer& other) = delete;
  imgui_layer& operator=(imgui_layer& other) = delete;

  imgui_layer(imgui_layer&& other) noexcept = delete;
  imgui_layer& operator=(imgui_layer&& other) noexcept = delete;

  void handle_event(const SDL_Event& event) noexcept;
  void update_render_target(imgui_render_target render_target);
  void begin_frame();
  void draw_renderer_panel(const rl::vulkan::renderer_status& status, rl::vulkan::renderer_settings& settings);
  void end_frame();
  void render(VkCommandBuffer command_buffer);

 private:
  void* context_ = nullptr;
  VkDevice device_ = VK_NULL_HANDLE;
  imgui_render_target render_target_{};
};

}  // namespace rl::ui
