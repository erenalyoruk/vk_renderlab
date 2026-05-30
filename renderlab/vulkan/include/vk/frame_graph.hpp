#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

namespace rl::vulkan {

enum class frame_graph_queue : std::uint8_t {
  graphics,
  compute,
  transfer,
};

enum class frame_graph_resource_type : std::uint8_t {
  swapchain_image,
  color_image,
  depth_image,
  buffer,
};

enum class frame_graph_access_type : std::uint8_t {
  read,
  write,
  read_write,
};

struct frame_graph_resource {
  std::string name;
  frame_graph_resource_type type = frame_graph_resource_type::color_image;
  bool imported = false;
};

struct frame_graph_access {
  std::string resource;
  frame_graph_access_type access = frame_graph_access_type::read;
};

struct frame_graph_context {
  std::reference_wrapper<const vk::raii::CommandBuffer> command_buffer;
};

using frame_graph_execute_callback = std::function<void(const frame_graph_context&)>;

struct frame_graph_pass {
  std::string name;
  frame_graph_queue queue = frame_graph_queue::graphics;
  std::vector<frame_graph_access> accesses;
  frame_graph_execute_callback execute;
};

class frame_graph final {
 public:
  void clear() noexcept;

  void import_resource(frame_graph_resource resource);
  void add_pass(frame_graph_pass pass);
  void compile();
  void execute(const vk::raii::CommandBuffer& command_buffer) const;

  [[nodiscard]] std::span<const frame_graph_resource> resources() const noexcept;
  [[nodiscard]] std::span<const frame_graph_pass> passes() const noexcept;
  [[nodiscard]] bool compiled() const noexcept;

 private:
  [[nodiscard]] bool has_resource(std::string_view name) const noexcept;
  [[nodiscard]] bool has_pass(std::string_view name) const noexcept;

  std::vector<frame_graph_resource> resources_;
  std::vector<frame_graph_pass> passes_;
  bool compiled_ = false;
};

[[nodiscard]] std::string_view to_string(frame_graph_queue queue) noexcept;
[[nodiscard]] std::string_view to_string(frame_graph_resource_type type) noexcept;
[[nodiscard]] std::string_view to_string(frame_graph_access_type access) noexcept;

}  // namespace rl::vulkan
