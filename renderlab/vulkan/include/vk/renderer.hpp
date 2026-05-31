#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

#include "base/noncopyable.hpp"
#include "platform/platform_event.hpp"
#include "vk/frame_graph.hpp"
#include "vk/memory_allocator.hpp"
#include "vk/pipeline.hpp"
#include "vk/render_path.hpp"

namespace rl::vulkan {

class vulkan_context;

struct renderer_settings {
  std::array<float, 4> clear_color{0.06f, 0.09f, 0.14f, 1.0f};
  vk::PresentModeKHR preferred_present_mode = vk::PresentModeKHR::eMailbox;
  render_path path = render_path::forward_plus;
  std::uint32_t max_frames_in_flight = 2;
  bool recreate_swapchain_on_suboptimal = true;
};

struct renderer_status {
  platform::extent2d drawable_extent{};
  vk::Extent2D swapchain_extent{};
  std::uint32_t swapchain_image_count = 0;
  std::uint64_t swapchain_generation = 0;
  std::uint64_t frame_index = 0;
  std::uint32_t frame_graph_pass_count = 0;
  render_path path = render_path::forward_plus;
  vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
  bool suspended = false;
  bool swapchain_ready = false;
};

struct renderer_ui_render_target {
  VkFormat color_format = VK_FORMAT_UNDEFINED;
  std::uint32_t min_image_count = 2;
  std::uint32_t image_count = 2;
  std::uint64_t generation = 0;
};

class renderer final : public noncopyable {
 public:
  using overlay_record_callback = std::function<void(VkCommandBuffer)>;

  renderer(const vulkan_context& context, platform::extent2d drawable_extent, renderer_settings settings = {});
  ~renderer() noexcept;

  renderer(renderer& other) = delete;
  renderer& operator=(renderer& other) = delete;

  renderer(renderer&& other) noexcept = delete;
  renderer& operator=(renderer&& other) noexcept = delete;

  void handle_event(const platform::platform_event& event);
  void set_drawable_extent(platform::extent2d drawable_extent);
  void set_suspended(bool suspended);
  void set_preferred_present_mode(vk::PresentModeKHR present_mode);
  void apply_pending_settings();
  void draw_frame(const overlay_record_callback& overlay = {});

  [[nodiscard]] renderer_settings& settings() noexcept;
  [[nodiscard]] const renderer_settings& settings() const noexcept;
  [[nodiscard]] renderer_status status() const noexcept;
  [[nodiscard]] renderer_ui_render_target ui_render_target() const noexcept;
  [[nodiscard]] bool suspended() const noexcept;

 private:
  struct frame_resources {
    vk::raii::CommandBuffer command_buffer{nullptr};
    vk::raii::Semaphore image_available{nullptr};
    std::uint64_t timeline_value = 0;
  };

  [[nodiscard]] bool has_drawable_extent() const noexcept;
  [[nodiscard]] bool has_depth_target(std::size_t image_index) const noexcept;
  [[nodiscard]] bool can_render() const noexcept;
  [[nodiscard]] std::uint32_t frame_count() const noexcept;
  [[nodiscard]] bool wait_for_timeline_value(std::uint64_t value) const;

  void create_command_pool();
  void create_frame_resources();
  void recreate_swapchain();
  void release_swapchain() noexcept;
  void create_swapchain_image_views();
  void create_depth_resources();
  void create_debug_pipeline();
  void build_frame_graph(std::size_t image_index);
  void record_frame_commands(frame_resources& frame, std::size_t image_index, const overlay_record_callback& overlay);
  void record_overlay_commands(const vk::raii::CommandBuffer& command_buffer, std::size_t image_index,
                               const overlay_record_callback& overlay);
  void draw_frame_impl(const overlay_record_callback& overlay);
  void wait_device_idle() noexcept;

  const vulkan_context& context_;
  renderer_settings settings_;

  vk::raii::CommandPool command_pool_{nullptr};
  std::vector<frame_resources> frames_;
  vk::raii::Semaphore frame_timeline_{nullptr};

  vk::raii::SwapchainKHR swapchain_{nullptr};
  std::vector<vk::Image> swapchain_images_;
  std::vector<vk::raii::ImageView> swapchain_image_views_;
  std::vector<vk::ImageLayout> image_layouts_;
  std::vector<vk::raii::Semaphore> render_finished_;
  std::vector<std::uint64_t> image_timeline_values_;
  std::vector<gpu_image> depth_images_;
  std::vector<vk::raii::ImageView> depth_image_views_;
  std::vector<vk::ImageLayout> depth_layouts_;
  std::optional<shader_module> debug_vertex_shader_;
  std::optional<shader_module> debug_fragment_shader_;
  std::optional<graphics_pipeline> debug_triangle_pipeline_;

  platform::extent2d drawable_extent_{};
  vk::SurfaceFormatKHR surface_format_{};
  std::uint32_t swapchain_min_image_count_ = 2;
  vk::Format depth_format_ = vk::Format::eUndefined;
  vk::PresentModeKHR present_mode_ = vk::PresentModeKHR::eFifo;
  vk::Extent2D swapchain_extent_{};
  std::uint64_t swapchain_generation_ = 0;
  frame_graph frame_graph_;

  std::size_t current_frame_ = 0;
  std::uint64_t frame_index_ = 0;
  std::uint64_t next_timeline_value_ = 1;
  bool suspended_ = false;
  bool swapchain_recreate_requested_ = false;
};

}  // namespace rl::vulkan
