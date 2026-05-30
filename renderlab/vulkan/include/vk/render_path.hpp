#pragma once

#include <cstdint>
#include <string_view>

#include <vulkan/vulkan_raii.hpp>

#include "vk/frame_graph.hpp"

namespace rl::vulkan {

enum class render_path : std::uint8_t {
  forward,
  forward_plus,
  deferred,
};

struct render_path_capabilities {
  bool bindless_descriptors = false;
  bool mesh_shader = false;
  bool task_shader = false;
};

struct render_path_swapchain_target {
  vk::Image image{};
  vk::ImageView image_view{};
  vk::ImageLayout old_layout = vk::ImageLayout::eUndefined;
  vk::Extent2D extent{};
  vk::ClearColorValue clear_color{};
};

struct render_path_build_info {
  render_path path = render_path::forward_plus;
  render_path_capabilities capabilities{};
  render_path_swapchain_target swapchain{};
};

void build_render_path_frame_graph(frame_graph& graph, const render_path_build_info& info);

[[nodiscard]] std::string_view to_string(render_path path) noexcept;

}  // namespace rl::vulkan
