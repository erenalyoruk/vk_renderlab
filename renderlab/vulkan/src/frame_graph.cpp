#include "vk/frame_graph.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <vulkan/vulkan_raii.hpp>

namespace rl::vulkan {
namespace {

void require_name(std::string_view name, std::string_view object_type) {
  if (name.empty()) {
    throw std::runtime_error{std::string{object_type} + " name must not be empty"};
  }
}

}  // namespace

void frame_graph::clear() noexcept {
  resources_.clear();
  passes_.clear();
  compiled_ = false;
}

void frame_graph::import_resource(frame_graph_resource resource) {
  require_name(resource.name, "frame graph resource");

  if (has_resource(resource.name)) {
    throw std::runtime_error{"duplicate frame graph resource: " + resource.name};
  }

  resource.imported = true;
  resources_.push_back(std::move(resource));
  compiled_ = false;
}

void frame_graph::add_pass(frame_graph_pass pass) {
  require_name(pass.name, "frame graph pass");

  if (has_pass(pass.name)) {
    throw std::runtime_error{"duplicate frame graph pass: " + pass.name};
  }

  if (!pass.execute) {
    throw std::runtime_error{"frame graph pass has no execute callback: " + pass.name};
  }

  passes_.push_back(std::move(pass));
  compiled_ = false;
}

void frame_graph::compile() {
  for (const frame_graph_pass& pass : passes_) {
    for (const frame_graph_access& access : pass.accesses) {
      require_name(access.resource, "frame graph access resource");

      if (!has_resource(access.resource)) {
        throw std::runtime_error{"frame graph pass '" + pass.name +
                                 "' references unknown resource: " + access.resource};
      }
    }
  }

  compiled_ = true;
}

void frame_graph::execute(const vk::raii::CommandBuffer& command_buffer) const {
  if (!compiled_) {
    throw std::runtime_error{"frame graph must be compiled before execution"};
  }

  const frame_graph_context context{
    .command_buffer = command_buffer,
  };

  for (const frame_graph_pass& pass : passes_) {
    pass.execute(context);
  }
}

std::span<const frame_graph_resource> frame_graph::resources() const noexcept { return resources_; }

std::span<const frame_graph_pass> frame_graph::passes() const noexcept { return passes_; }

bool frame_graph::compiled() const noexcept { return compiled_; }

bool frame_graph::has_resource(std::string_view name) const noexcept {
  return std::ranges::any_of(resources_,
                             [name](const frame_graph_resource& resource) { return resource.name == name; });
}

bool frame_graph::has_pass(std::string_view name) const noexcept {
  return std::ranges::any_of(passes_, [name](const frame_graph_pass& pass) { return pass.name == name; });
}

std::string_view to_string(frame_graph_queue queue) noexcept {
  switch (queue) {
    case frame_graph_queue::graphics:
      return "graphics";
    case frame_graph_queue::compute:
      return "compute";
    case frame_graph_queue::transfer:
      return "transfer";
  }

  return "unknown";
}

std::string_view to_string(frame_graph_resource_type type) noexcept {
  switch (type) {
    case frame_graph_resource_type::swapchain_image:
      return "swapchain_image";
    case frame_graph_resource_type::color_image:
      return "color_image";
    case frame_graph_resource_type::depth_image:
      return "depth_image";
    case frame_graph_resource_type::buffer:
      return "buffer";
  }

  return "unknown";
}

std::string_view to_string(frame_graph_access_type access) noexcept {
  switch (access) {
    case frame_graph_access_type::read:
      return "read";
    case frame_graph_access_type::write:
      return "write";
    case frame_graph_access_type::read_write:
      return "read_write";
  }

  return "unknown";
}

}  // namespace rl::vulkan
