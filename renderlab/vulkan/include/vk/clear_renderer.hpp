#pragma once

#include <cstddef>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "base/noncopyable.hpp"
#include "platform/platform_event.hpp"

namespace rl::vulkan {

class vulkan_context;

class clear_renderer final : public noncopyable {
 public:
  clear_renderer(const vulkan_context& context, platform::extent2d drawable_extent);
  ~clear_renderer() noexcept;

  clear_renderer(clear_renderer& other) = delete;
  clear_renderer& operator=(clear_renderer& other) = delete;

  clear_renderer(clear_renderer&& other) noexcept = delete;
  clear_renderer& operator=(clear_renderer&& other) noexcept = delete;

  void resize(platform::extent2d drawable_extent);
  void draw_frame();

 private:
  [[nodiscard]] bool has_drawable_extent() const noexcept;

  void create_command_pool();
  void create_sync_objects();
  void recreate_swapchain();
  void release_swapchain() noexcept;
  void record_clear_commands(std::size_t image_index);
  void draw_frame_impl();

  const vulkan_context& context_;

  vk::raii::CommandPool command_pool_{nullptr};
  std::vector<vk::raii::CommandBuffer> command_buffers_;

  vk::raii::Semaphore image_available_{nullptr};
  std::vector<vk::raii::Semaphore> render_finished_;
  vk::raii::Fence in_flight_{nullptr};

  vk::raii::SwapchainKHR swapchain_{nullptr};
  std::vector<vk::Image> swapchain_images_;
  std::vector<vk::ImageLayout> image_layouts_;

  platform::extent2d drawable_extent_{};
  vk::SurfaceFormatKHR surface_format_{};
  vk::Extent2D swapchain_extent_{};
  vk::ClearColorValue clear_color_{std::array{0.06f, 0.09f, 0.14f, 1.0f}};
};

}  // namespace rl::vulkan
