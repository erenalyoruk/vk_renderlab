#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "base/noncopyable.hpp"
#include "platform/platform_event.hpp"

namespace rl::vulkan {

class vulkan_context;

struct renderer_settings {
  vk::ClearColorValue clear_color{std::array{0.06f, 0.09f, 0.14f, 1.0f}};
  vk::PresentModeKHR preferred_present_mode = vk::PresentModeKHR::eMailbox;
  std::uint32_t max_frames_in_flight = 2;
  bool recreate_swapchain_on_suboptimal = true;
};

struct renderer_status {
  platform::extent2d drawable_extent{};
  vk::Extent2D swapchain_extent{};
  std::uint32_t swapchain_image_count = 0;
  std::uint64_t frame_index = 0;
  bool suspended = false;
  bool swapchain_ready = false;
};

class renderer final : public noncopyable {
 public:
  renderer(const vulkan_context& context, platform::extent2d drawable_extent, renderer_settings settings = {});
  ~renderer() noexcept;

  renderer(renderer& other) = delete;
  renderer& operator=(renderer& other) = delete;

  renderer(renderer&& other) noexcept = delete;
  renderer& operator=(renderer&& other) noexcept = delete;

  void handle_event(const platform::platform_event& event);
  void set_drawable_extent(platform::extent2d drawable_extent);
  void set_suspended(bool suspended);
  void draw_frame();

  [[nodiscard]] renderer_settings& settings() noexcept;
  [[nodiscard]] const renderer_settings& settings() const noexcept;
  [[nodiscard]] renderer_status status() const noexcept;
  [[nodiscard]] bool suspended() const noexcept;

 private:
  struct frame_resources {
    vk::raii::CommandBuffer command_buffer{nullptr};
    vk::raii::Semaphore image_available{nullptr};
    vk::raii::Fence in_flight{nullptr};
  };

  [[nodiscard]] bool has_drawable_extent() const noexcept;
  [[nodiscard]] bool can_render() const noexcept;
  [[nodiscard]] std::uint32_t frame_count() const noexcept;

  void create_command_pool();
  void create_frame_resources();
  void recreate_swapchain();
  void release_swapchain() noexcept;
  void create_swapchain_image_views();
  void record_frame_commands(frame_resources& frame, std::size_t image_index);
  void draw_frame_impl();
  void wait_device_idle() noexcept;

  const vulkan_context& context_;
  renderer_settings settings_;

  vk::raii::CommandPool command_pool_{nullptr};
  std::vector<frame_resources> frames_;

  vk::raii::SwapchainKHR swapchain_{nullptr};
  std::vector<vk::Image> swapchain_images_;
  std::vector<vk::raii::ImageView> swapchain_image_views_;
  std::vector<vk::ImageLayout> image_layouts_;
  std::vector<vk::raii::Semaphore> render_finished_;
  std::vector<vk::Fence> image_in_flight_fences_;

  platform::extent2d drawable_extent_{};
  vk::SurfaceFormatKHR surface_format_{};
  vk::PresentModeKHR present_mode_ = vk::PresentModeKHR::eFifo;
  vk::Extent2D swapchain_extent_{};

  std::size_t current_frame_ = 0;
  std::uint64_t frame_index_ = 0;
  bool suspended_ = false;
};

}  // namespace rl::vulkan
