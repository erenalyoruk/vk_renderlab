#include "vk/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "assets/file_io.hpp"
#include "base/log.hpp"
#include "vk/device.hpp"
#include "vk/frame_graph.hpp"
#include "vk/memory_allocator.hpp"
#include "vk/pipeline.hpp"
#include "vk/render_path.hpp"
#include "vk/upload_context.hpp"
#include "vk/vulkan_context.hpp"

namespace rl::vulkan {
namespace {

constexpr std::uint32_t min_frames_in_flight = 1;
constexpr std::uint32_t max_frames_in_flight = 3;
constexpr std::uint64_t bytes_per_mib = std::uint64_t{1024} * std::uint64_t{1024};
constexpr std::string_view debug_cube_vertex_shader_path = "renderlab/shaders/debug_cube.vert.spv";
constexpr std::string_view debug_cube_fragment_shader_path = "renderlab/shaders/debug_cube.frag.spv";
constexpr float vertical_fov_radians = glm::pi<float>() / 3.0f;

struct debug_vertex {
  glm::vec3 position;
  glm::vec3 color;
};

struct scene_uniforms {
  glm::mat4 mvp{1.0f};
};

struct camera_parameters {
  glm::vec3 position;
  glm::vec3 target;
  glm::vec3 up;
  float vertical_fov = vertical_fov_radians;
  float near_plane = 0.1f;
  float far_plane = 100.0f;
};

constexpr std::array debug_cube_vertices{
  debug_vertex{.position = {-1.0f, -1.0f, -1.0f}, .color = {1.0f, 0.1f, 0.1f}},
  debug_vertex{.position = {1.0f, -1.0f, -1.0f}, .color = {0.1f, 1.0f, 0.1f}},
  debug_vertex{.position = {1.0f, 1.0f, -1.0f}, .color = {0.1f, 0.3f, 1.0f}},
  debug_vertex{.position = {-1.0f, 1.0f, -1.0f}, .color = {1.0f, 0.9f, 0.1f}},
  debug_vertex{.position = {-1.0f, -1.0f, 1.0f}, .color = {1.0f, 0.1f, 0.9f}},
  debug_vertex{.position = {1.0f, -1.0f, 1.0f}, .color = {0.1f, 0.9f, 1.0f}},
  debug_vertex{.position = {1.0f, 1.0f, 1.0f}, .color = {0.9f, 0.9f, 0.9f}},
  debug_vertex{.position = {-1.0f, 1.0f, 1.0f}, .color = {0.4f, 0.8f, 0.2f}},
};

constexpr std::array<std::uint16_t, 36> debug_cube_indices{
  0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4,
};

[[nodiscard]] bool extent_has_area(platform::extent2d extent) noexcept { return extent.width > 0 && extent.height > 0; }

[[nodiscard]] std::uint32_t clamp_frame_count(std::uint32_t frame_count) noexcept {
  return std::clamp(frame_count, min_frames_in_flight, max_frames_in_flight);
}

[[nodiscard]] std::uint64_t bytes_to_mib(std::uint64_t bytes) noexcept { return bytes / bytes_per_mib; }

[[nodiscard]] camera_parameters make_debug_camera() noexcept {
  return camera_parameters{
    .position = {2.0f, 2.0f, 4.0f},
    .target = {0.0f, 0.0f, 0.0f},
    .up = {0.0f, 1.0f, 0.0f},
  };
}

[[nodiscard]] float aspect_ratio(vk::Extent2D extent) noexcept {
  return static_cast<float>(extent.width) / static_cast<float>(std::max(extent.height, 1u));
}

[[nodiscard]] glm::mat4 make_vulkan_projection(const camera_parameters& camera, vk::Extent2D extent) {
  const glm::mat4 projection =
      glm::perspective(camera.vertical_fov, aspect_ratio(extent), camera.near_plane, camera.far_plane);
  const glm::mat4 vulkan_clip = glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, -1.0f, 1.0f});
  return vulkan_clip * projection;
}

[[nodiscard]] glm::mat4 make_view_projection(const camera_parameters& camera, vk::Extent2D extent) {
  const glm::mat4 projection = make_vulkan_projection(camera, extent);
  const glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
  return projection * view;
}

[[nodiscard]] glm::mat4 make_debug_cube_model(std::uint64_t frame_index) {
  const float rotation = static_cast<float>(frame_index) * 0.015f;
  return glm::rotate(glm::mat4{1.0f}, rotation, glm::vec3{0.0f, 1.0f, 0.0f});
}

template <typename Value>
void write_mapped_value(gpu_buffer& buffer, const Value& value) {
  const std::span<std::byte> mapped_bytes = buffer.mapped_bytes();
  if (mapped_bytes.size_bytes() < sizeof(Value)) {
    throw std::runtime_error{"mapped buffer is too small for value upload"};
  }

  std::memcpy(mapped_bytes.data(), &value, sizeof(Value));
  buffer.flush();
}

[[nodiscard]] vk::VertexInputBindingDescription make_debug_vertex_binding() noexcept {
  vk::VertexInputBindingDescription result{};
  result.binding = 0;
  result.stride = sizeof(debug_vertex);
  result.inputRate = vk::VertexInputRate::eVertex;
  return result;
}

[[nodiscard]] std::array<vk::VertexInputAttributeDescription, 2> make_debug_vertex_attributes() noexcept {
  std::array<vk::VertexInputAttributeDescription, 2> result{};
  result.at(0).location = 0;
  result.at(0).binding = 0;
  result.at(0).format = vk::Format::eR32G32B32Sfloat;
  result.at(0).offset = offsetof(debug_vertex, position);
  result.at(1).location = 1;
  result.at(1).binding = 0;
  result.at(1).format = vk::Format::eR32G32B32Sfloat;
  result.at(1).offset = offsetof(debug_vertex, color);
  return result;
}

[[nodiscard]] vk::SurfaceFormatKHR choose_surface_format(const std::vector<vk::SurfaceFormatKHR>& formats) {
  const auto preferred = std::ranges::find_if(formats, [](const vk::SurfaceFormatKHR& format) {
    return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
  });

  if (preferred != formats.end()) {
    return *preferred;
  }

  return formats.at(0);
}

[[nodiscard]] vk::PresentModeKHR choose_present_mode(const std::vector<vk::PresentModeKHR>& present_modes,
                                                     vk::PresentModeKHR preferred_present_mode) {
  if (std::ranges::contains(present_modes, preferred_present_mode)) {
    return preferred_present_mode;
  }

  return vk::PresentModeKHR::eFifo;
}

[[nodiscard]] bool requires_unenabled_present_mode_extension(
    vk::PresentModeKHR present_mode, const optional_device_features& optional_features) noexcept {
  return present_mode == vk::PresentModeKHR::eFifoLatestReadyEXT && !optional_features.present_mode_fifo_latest_ready;
}

[[nodiscard]] std::vector<vk::PresentModeKHR> filter_usable_present_modes(
    const std::vector<vk::PresentModeKHR>& present_modes, const optional_device_features& optional_features) {
  std::vector<vk::PresentModeKHR> usable_present_modes;
  usable_present_modes.reserve(present_modes.size());

  for (const vk::PresentModeKHR present_mode : present_modes) {
    if (requires_unenabled_present_mode_extension(present_mode, optional_features)) {
      RL_RENDER_DEBUG("ignoring present mode requiring an unenabled extension: {}", vk::to_string(present_mode));
      continue;
    }

    usable_present_modes.push_back(present_mode);
  }

  return usable_present_modes;
}

[[nodiscard]] vk::Extent2D choose_swapchain_extent(const vk::SurfaceCapabilitiesKHR& capabilities,
                                                   platform::extent2d drawable_extent) {
  if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
    return capabilities.currentExtent;
  }

  vk::Extent2D result{};
  result.width = std::clamp(static_cast<std::uint32_t>(drawable_extent.width), capabilities.minImageExtent.width,
                            capabilities.maxImageExtent.width);
  result.height = std::clamp(static_cast<std::uint32_t>(drawable_extent.height), capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height);
  return result;
}

[[nodiscard]] std::uint32_t choose_image_count(const vk::SurfaceCapabilitiesKHR& capabilities) noexcept {
  std::uint32_t image_count = capabilities.minImageCount + 1u;

  if (capabilities.maxImageCount > 0u) {
    image_count = std::min(image_count, capabilities.maxImageCount);
  }

  return image_count;
}

[[nodiscard]] vk::ImageViewCreateInfo make_swapchain_image_view_create_info(vk::Image image,
                                                                            vk::Format format) noexcept {
  vk::ImageViewCreateInfo create_info{};
  create_info.image = image;
  create_info.viewType = vk::ImageViewType::e2D;
  create_info.format = format;
  create_info.components.r = vk::ComponentSwizzle::eIdentity;
  create_info.components.g = vk::ComponentSwizzle::eIdentity;
  create_info.components.b = vk::ComponentSwizzle::eIdentity;
  create_info.components.a = vk::ComponentSwizzle::eIdentity;
  create_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  create_info.subresourceRange.baseMipLevel = 0;
  create_info.subresourceRange.levelCount = 1;
  create_info.subresourceRange.baseArrayLayer = 0;
  create_info.subresourceRange.layerCount = 1;
  return create_info;
}

[[nodiscard]] vk::ImageSubresourceRange color_subresource_range() noexcept {
  vk::ImageSubresourceRange result{};
  result.aspectMask = vk::ImageAspectFlagBits::eColor;
  result.baseMipLevel = 0;
  result.levelCount = 1;
  result.baseArrayLayer = 0;
  result.layerCount = 1;
  return result;
}

[[nodiscard]] vk::ImageMemoryBarrier2 make_color_layout_barrier(
    vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout, vk::PipelineStageFlags2 src_stage,
    vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access) noexcept {
  vk::ImageMemoryBarrier2 barrier{};
  barrier.srcStageMask = src_stage;
  barrier.srcAccessMask = src_access;
  barrier.dstStageMask = dst_stage;
  barrier.dstAccessMask = dst_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.image = image;
  barrier.subresourceRange = color_subresource_range();
  return barrier;
}

[[nodiscard]] vk::ImageViewCreateInfo make_depth_image_view_create_info(vk::Image image, vk::Format format) noexcept {
  vk::ImageViewCreateInfo create_info{};
  create_info.image = image;
  create_info.viewType = vk::ImageViewType::e2D;
  create_info.format = format;
  create_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
  create_info.subresourceRange.baseMipLevel = 0;
  create_info.subresourceRange.levelCount = 1;
  create_info.subresourceRange.baseArrayLayer = 0;
  create_info.subresourceRange.layerCount = 1;
  return create_info;
}

[[nodiscard]] vk::Format choose_depth_format(const vk::raii::PhysicalDevice& physical_device) {
  constexpr std::array candidates = {
    vk::Format::eD32Sfloat,
    vk::Format::eD32SfloatS8Uint,
    vk::Format::eD24UnormS8Uint,
  };

  for (const vk::Format format : candidates) {
    const vk::FormatProperties properties = physical_device.getFormatProperties(format);
    if (static_cast<bool>(properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)) {
      return format;
    }
  }

  throw std::runtime_error{"no supported Vulkan depth attachment format found"};
}

[[nodiscard]] platform::extent2d event_extent(const platform::platform_event& event) noexcept {
  const auto* resize = std::get_if<platform::window_extent_event>(&event.payload);
  if (resize == nullptr) {
    return {};
  }

  return resize->size;
}

[[nodiscard]] std::vector<std::byte> load_shader_bytes(std::string_view workspace_relative_path) {
  const std::filesystem::path path = assets::resolve_runfile(workspace_relative_path);
  std::vector<std::byte> bytecode = assets::read_binary_file(path);

  RL_SHADER_INFO("loaded shader module input: '{}' ({} bytes)", path.string(), bytecode.size());
  return bytecode;
}

}  // namespace

renderer::renderer(const vulkan_context& context, platform::extent2d drawable_extent, renderer_settings settings)
    : context_{context},
      settings_{settings},
      gpu_name_{physical_device_name(context.selected_physical_device().properties)},
      gpu_type_{physical_device_type_to_string(context.selected_physical_device().properties.deviceType)},
      gpu_api_version_{api_version_to_string(context.selected_physical_device().properties.apiVersion)},
      gpu_local_memory_mib_{
        bytes_to_mib(device_local_memory_bytes(context.selected_physical_device().memory_properties))},
      drawable_extent_{drawable_extent} {
  settings_.max_frames_in_flight = clamp_frame_count(settings_.max_frames_in_flight);
  create_command_pool();
  upload_context_.emplace(context_);
  create_frame_resources();
  create_debug_scene_resources();
  recreate_swapchain();
}

renderer::~renderer() noexcept { wait_device_idle(); }

void renderer::handle_event(const platform::platform_event& event) {
  switch (event.type) {
    case platform::platform_event_type::drawable_resized:
      set_drawable_extent(event_extent(event));
      break;
    case platform::platform_event_type::window_minimized:
    case platform::platform_event_type::window_hidden:
      set_suspended(true);
      break;
    case platform::platform_event_type::window_restored:
    case platform::platform_event_type::window_shown:
      set_suspended(false);
      break;
    default:
      break;
  }
}

void renderer::set_drawable_extent(platform::extent2d drawable_extent) {
  if (drawable_extent.width == drawable_extent_.width && drawable_extent.height == drawable_extent_.height) {
    return;
  }

  drawable_extent_ = drawable_extent;

  if (suspended_) {
    return;
  }

  wait_device_idle();
  recreate_swapchain();
}

void renderer::set_suspended(bool suspended) {
  if (suspended_ == suspended) {
    return;
  }

  suspended_ = suspended;

  if (suspended_) {
    wait_device_idle();
    RL_RENDER_INFO("renderer suspended");
    return;
  }

  RL_RENDER_INFO("renderer resumed");
  recreate_swapchain();
}

void renderer::set_preferred_present_mode(vk::PresentModeKHR present_mode) {
  if (settings_.preferred_present_mode == present_mode) {
    return;
  }

  settings_.preferred_present_mode = present_mode;
  swapchain_recreate_requested_ = true;
}

void renderer::set_max_frames_in_flight(std::uint32_t frames_in_flight) {
  const std::uint32_t clamped_frames_in_flight = clamp_frame_count(frames_in_flight);
  if (settings_.max_frames_in_flight == clamped_frames_in_flight) {
    return;
  }

  wait_device_idle();

  settings_.max_frames_in_flight = clamped_frames_in_flight;
  current_frame_ = 0;
  next_timeline_value_ = 1;
  image_timeline_values_.assign(swapchain_images_.size(), 0);
  create_frame_resources();
  create_debug_scene_descriptor_sets();
}

void renderer::apply_pending_settings() {
  if (!swapchain_recreate_requested_ || suspended_ || !has_drawable_extent()) {
    return;
  }

  wait_device_idle();
  recreate_swapchain();
}

void renderer::draw_frame(const overlay_record_callback& overlay) {
  if (!can_render()) {
    return;
  }

  try {
    draw_frame_impl(overlay);
  } catch (const vk::OutOfDateKHRError&) {
    wait_device_idle();
    recreate_swapchain();
  }
}

renderer_settings& renderer::settings() noexcept { return settings_; }

const renderer_settings& renderer::settings() const noexcept { return settings_; }

renderer_status renderer::status() const noexcept {
  return renderer_status{
    .gpu_name = gpu_name_,
    .gpu_type = gpu_type_,
    .gpu_api_version = gpu_api_version_,
    .gpu_local_memory_mib = gpu_local_memory_mib_,
    .drawable_extent = drawable_extent_,
    .swapchain_extent = swapchain_extent_,
    .swapchain_image_count = static_cast<std::uint32_t>(swapchain_images_.size()),
    .swapchain_generation = swapchain_generation_,
    .frame_index = frame_index_,
    .frame_graph_pass_count = static_cast<std::uint32_t>(frame_graph_.passes().size()),
    .frames_in_flight = static_cast<std::uint32_t>(frames_.size()),
    .path = settings_.path,
    .preferred_present_mode = settings_.preferred_present_mode,
    .present_mode = present_mode_,
    .suspended = suspended_,
    .swapchain_ready = static_cast<bool>(*swapchain_) && has_drawable_extent(),
  };
}

renderer_ui_render_target renderer::ui_render_target() const noexcept {
  return renderer_ui_render_target{
    .color_format = static_cast<VkFormat>(surface_format_.format),
    .min_image_count = swapchain_min_image_count_,
    .image_count = static_cast<std::uint32_t>(swapchain_images_.size()),
    .generation = swapchain_generation_,
  };
}

const std::vector<vk::PresentModeKHR>& renderer::supported_present_modes() const noexcept {
  return supported_present_modes_;
}

bool renderer::suspended() const noexcept { return suspended_; }

bool renderer::has_drawable_extent() const noexcept { return extent_has_area(drawable_extent_); }

bool renderer::has_depth_target(std::size_t image_index) const noexcept {
  if (image_index >= depth_images_.size() || image_index >= depth_image_views_.size()) {
    return false;
  }

  return static_cast<bool>(depth_images_.at(image_index)) && static_cast<bool>(*depth_image_views_.at(image_index));
}

bool renderer::can_render() const noexcept {
  return !suspended_ && has_drawable_extent() && static_cast<bool>(*swapchain_);
}

std::uint32_t renderer::frame_count() const noexcept { return clamp_frame_count(settings_.max_frames_in_flight); }

bool renderer::has_debug_cube_mesh() const noexcept {
  return debug_cube_mesh_.vertex_buffer && debug_cube_mesh_.index_buffer && debug_cube_mesh_.index_count > 0;
}

bool renderer::wait_for_timeline_value(std::uint64_t value) const {
  if (value == 0) {
    return true;
  }

  const std::array semaphores = {*frame_timeline_};
  const std::array values = {value};

  vk::SemaphoreWaitInfo wait_info{};
  wait_info.setSemaphores(semaphores);
  wait_info.setValues(values);

  const vk::Result result =
      context_.device().handle().waitSemaphores(wait_info, std::numeric_limits<std::uint64_t>::max());
  return result == vk::Result::eSuccess;
}

void renderer::create_command_pool() {
  const auto graphics_queue = context_.device().queues().graphics;
  if (!graphics_queue.has_value()) {
    throw std::runtime_error{"renderer requires a graphics queue"};
  }

  vk::CommandPoolCreateInfo create_info{};
  create_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  create_info.queueFamilyIndex = *graphics_queue;

  command_pool_ = vk::raii::CommandPool{context_.device().handle(), create_info};
}

void renderer::create_frame_resources() {
  vk::SemaphoreTypeCreateInfo timeline_type{};
  timeline_type.semaphoreType = vk::SemaphoreType::eTimeline;
  timeline_type.initialValue = 0;

  vk::SemaphoreCreateInfo timeline_create_info{};
  timeline_create_info.pNext = &timeline_type;
  frame_timeline_ = vk::raii::Semaphore{context_.device().handle(), timeline_create_info};

  vk::CommandBufferAllocateInfo allocate_info{};
  allocate_info.commandPool = *command_pool_;
  allocate_info.level = vk::CommandBufferLevel::ePrimary;
  allocate_info.commandBufferCount = frame_count();

  std::vector<vk::raii::CommandBuffer> command_buffers =
      context_.device().handle().allocateCommandBuffers(allocate_info);

  const vk::SemaphoreCreateInfo semaphore_create_info{};

  frames_.clear();
  frames_.reserve(command_buffers.size());

  for (vk::raii::CommandBuffer& command_buffer : command_buffers) {
    frames_.push_back(frame_resources{
      .command_buffer = std::move(command_buffer),
      .image_available = vk::raii::Semaphore{context_.device().handle(), semaphore_create_info},
      .scene_uniform_buffer = context_.allocator().create_buffer(buffer_description{
        .size = sizeof(scene_uniforms),
        .usage = vk::BufferUsageFlagBits::eUniformBuffer,
        .memory = memory_usage::cpu_to_gpu,
        .mapped = true,
      }),
    });
  }

  RL_RENDER_INFO("frame timeline semaphore ready: frames_in_flight={}", frames_.size());
}

void renderer::create_debug_scene_resources() {
  const std::span vertex_bytes = std::as_bytes(std::span{debug_cube_vertices});
  const std::span index_bytes = std::as_bytes(std::span{debug_cube_indices});
  if (!upload_context_.has_value()) {
    throw std::runtime_error{"renderer upload context is not initialized"};
  }

  const upload_context& uploader = upload_context_.value();
  debug_cube_mesh_.vertex_buffer = uploader.create_device_buffer(vertex_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
  debug_cube_mesh_.index_buffer = uploader.create_device_buffer(index_bytes, vk::BufferUsageFlagBits::eIndexBuffer);
  debug_cube_mesh_.index_count = static_cast<std::uint32_t>(debug_cube_indices.size());

  vk::DescriptorSetLayoutBinding scene_uniform_binding{};
  scene_uniform_binding.binding = 0;
  scene_uniform_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
  scene_uniform_binding.descriptorCount = 1;
  scene_uniform_binding.stageFlags = vk::ShaderStageFlagBits::eVertex;

  vk::DescriptorSetLayoutCreateInfo layout_create_info{};
  layout_create_info.setBindings(scene_uniform_binding);
  debug_scene_descriptor_set_layout_ = vk::raii::DescriptorSetLayout{context_.device().handle(), layout_create_info};

  create_debug_scene_descriptor_sets();
}

void renderer::create_debug_scene_descriptor_sets() {
  debug_scene_descriptor_sets_.clear();
  debug_scene_descriptor_pool_ = nullptr;

  if (frames_.empty() || !*debug_scene_descriptor_set_layout_) {
    return;
  }

  vk::DescriptorPoolSize pool_size{};
  pool_size.type = vk::DescriptorType::eUniformBuffer;
  pool_size.descriptorCount = static_cast<std::uint32_t>(frames_.size());

  vk::DescriptorPoolCreateInfo pool_create_info{};
  pool_create_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
  pool_create_info.maxSets = static_cast<std::uint32_t>(frames_.size());
  pool_create_info.setPoolSizes(pool_size);
  debug_scene_descriptor_pool_ = vk::raii::DescriptorPool{context_.device().handle(), pool_create_info};

  const std::vector layouts(frames_.size(), *debug_scene_descriptor_set_layout_);
  vk::DescriptorSetAllocateInfo allocate_info{};
  allocate_info.descriptorPool = *debug_scene_descriptor_pool_;
  allocate_info.setSetLayouts(layouts);
  debug_scene_descriptor_sets_ = context_.device().handle().allocateDescriptorSets(allocate_info);

  for (std::size_t frame_index = 0; frame_index < frames_.size(); ++frame_index) {
    vk::DescriptorBufferInfo buffer_info{};
    buffer_info.buffer = frames_.at(frame_index).scene_uniform_buffer.handle();
    buffer_info.offset = 0;
    buffer_info.range = sizeof(scene_uniforms);
    vk::WriteDescriptorSet write{};
    write.dstSet = *debug_scene_descriptor_sets_.at(frame_index);
    write.dstBinding = 0;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.setBufferInfo(buffer_info);
    context_.device().handle().updateDescriptorSets(write, nullptr);
  }
}

void renderer::recreate_swapchain() {
  release_swapchain();

  if (!has_drawable_extent()) {
    return;
  }

  const vk::raii::PhysicalDevice& physical_device = context_.selected_physical_device_handle();
  const vk::SurfaceKHR surface = context_.raw_surface();

  const vk::SurfaceCapabilitiesKHR capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);
  const std::vector<vk::SurfaceFormatKHR> formats = physical_device.getSurfaceFormatsKHR(surface);
  supported_present_modes_ = filter_usable_present_modes(physical_device.getSurfacePresentModesKHR(surface),
                                                         context_.device().optional_features());

  if (formats.empty() || supported_present_modes_.empty()) {
    throw std::runtime_error{"swapchain support is incomplete"};
  }

  surface_format_ = choose_surface_format(formats);
  present_mode_ = choose_present_mode(supported_present_modes_, settings_.preferred_present_mode);
  swapchain_extent_ = choose_swapchain_extent(capabilities, drawable_extent_);
  swapchain_min_image_count_ = std::max(2u, capabilities.minImageCount);

  vk::SwapchainCreateInfoKHR create_info{};
  create_info.surface = surface;
  create_info.minImageCount = choose_image_count(capabilities);
  create_info.imageFormat = surface_format_.format;
  create_info.imageColorSpace = surface_format_.colorSpace;
  create_info.imageExtent = swapchain_extent_;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  create_info.presentMode = present_mode_;
  create_info.clipped = vk::True;

  const queue_family_indices& queues = context_.device().queues();
  if (!queues.graphics.has_value() || !queues.present.has_value()) {
    throw std::runtime_error{"swapchain requires graphics and present queues"};
  }

  std::array queue_family_indices = {*queues.graphics, *queues.present};
  if (*queues.graphics != *queues.present) {
    create_info.imageSharingMode = vk::SharingMode::eConcurrent;
    create_info.setQueueFamilyIndices(queue_family_indices);
  } else {
    create_info.imageSharingMode = vk::SharingMode::eExclusive;
  }

  swapchain_ = vk::raii::SwapchainKHR{context_.device().handle(), create_info};
  swapchain_images_ = swapchain_.getImages();
  image_layouts_.assign(swapchain_images_.size(), vk::ImageLayout::eUndefined);
  image_timeline_values_.assign(swapchain_images_.size(), 0);

  create_swapchain_image_views();
  create_depth_resources();
  create_debug_pipeline();

  const vk::SemaphoreCreateInfo semaphore_create_info{};
  render_finished_.reserve(swapchain_images_.size());
  for (std::size_t image_index = 0; image_index < swapchain_images_.size(); ++image_index) {
    render_finished_.emplace_back(context_.device().handle(), semaphore_create_info);
  }

  ++swapchain_generation_;
  swapchain_recreate_requested_ = false;

  RL_RENDER_INFO("swapchain ready: {}x{}, images={}, frames_in_flight={}, present_mode={}", swapchain_extent_.width,
                 swapchain_extent_.height, swapchain_images_.size(), frames_.size(), vk::to_string(present_mode_));
}

void renderer::release_swapchain() noexcept {
  debug_cube_pipeline_.reset();
  depth_image_views_.clear();
  depth_images_.clear();
  depth_layouts_.clear();
  depth_format_ = vk::Format::eUndefined;
  render_finished_.clear();
  swapchain_image_views_.clear();
  image_timeline_values_.clear();
  image_layouts_.clear();
  swapchain_images_.clear();
  swapchain_ = nullptr;
}

void renderer::create_swapchain_image_views() {
  swapchain_image_views_.clear();
  swapchain_image_views_.reserve(swapchain_images_.size());

  for (const vk::Image image : swapchain_images_) {
    swapchain_image_views_.emplace_back(context_.device().handle(),
                                        make_swapchain_image_view_create_info(image, surface_format_.format));
  }
}

void renderer::create_depth_resources() {
  depth_format_ = choose_depth_format(context_.selected_physical_device_handle());
  depth_images_.clear();
  depth_image_views_.clear();
  depth_layouts_.clear();
  depth_images_.reserve(swapchain_images_.size());
  depth_image_views_.reserve(swapchain_images_.size());

  for (std::size_t image_count = 0; image_count < swapchain_images_.size(); ++image_count) {
    depth_images_.push_back(context_.allocator().create_image(image_description{
      .format = depth_format_,
      .extent = vk::Extent3D{swapchain_extent_.width, swapchain_extent_.height, 1},
      .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
      .memory = memory_usage::gpu_only,
    }));

    depth_image_views_.emplace_back(context_.device().handle(),
                                    make_depth_image_view_create_info(depth_images_.back().handle(), depth_format_));
  }

  depth_layouts_.assign(depth_images_.size(), vk::ImageLayout::eUndefined);

  RL_RENDER_INFO("depth targets ready: {}x{}, images={}, format={}", swapchain_extent_.width, swapchain_extent_.height,
                 depth_images_.size(), vk::to_string(depth_format_));
}

void renderer::create_debug_pipeline() {
  if (!debug_vertex_shader_.has_value()) {
    debug_vertex_shader_.emplace(context_.device().handle(), load_shader_bytes(debug_cube_vertex_shader_path));
  }

  if (!debug_fragment_shader_.has_value()) {
    debug_fragment_shader_.emplace(context_.device().handle(), load_shader_bytes(debug_cube_fragment_shader_path));
  }

  const std::array vertex_bindings = {make_debug_vertex_binding()};
  const std::array vertex_attributes = make_debug_vertex_attributes();
  const std::array descriptor_set_layouts = {*debug_scene_descriptor_set_layout_};

  debug_cube_pipeline_.emplace(context_.device().handle(), graphics_pipeline_description{
                                                             .vertex_shader = std::cref(*debug_vertex_shader_),
                                                             .fragment_shader = std::cref(*debug_fragment_shader_),
                                                             .vertex_bindings = vertex_bindings,
                                                             .vertex_attributes = vertex_attributes,
                                                             .descriptor_set_layouts = descriptor_set_layouts,
                                                             .color_format = surface_format_.format,
                                                             .depth_format = depth_format_,
                                                           });

  RL_SHADER_INFO("debug cube pipeline ready: color_format={}", vk::to_string(surface_format_.format));
}

void renderer::update_debug_scene_uniforms(std::size_t frame_index) {
  if (frame_index >= frames_.size() || swapchain_extent_.height == 0) {
    return;
  }

  const camera_parameters camera = make_debug_camera();
  const glm::mat4 view_projection = make_view_projection(camera, swapchain_extent_);
  const glm::mat4 model = make_debug_cube_model(frame_index_);

  write_mapped_value(frames_.at(frame_index).scene_uniform_buffer, scene_uniforms{
                                                                     .mvp = view_projection * model,
                                                                   });
}

void renderer::build_frame_graph(std::size_t image_index) {
  const optional_device_features& optional_features = context_.device().optional_features();
  const bool has_debug_draw =
      debug_cube_pipeline_.has_value() && has_debug_cube_mesh() && current_frame_ < debug_scene_descriptor_sets_.size();
  const std::optional<render_path_debug_draw> debug_draw =
      has_debug_draw ? std::optional<render_path_debug_draw>{render_path_debug_draw{
                         .pipeline = &*debug_cube_pipeline_,
                         .vertex_buffer = debug_cube_mesh_.vertex_buffer.handle(),
                         .index_buffer = debug_cube_mesh_.index_buffer.handle(),
                         .descriptor_set = *debug_scene_descriptor_sets_.at(current_frame_),
                         .index_count = debug_cube_mesh_.index_count,
                       }}
                     : std::nullopt;
  const std::optional<render_path_depth_target> depth_target =
      has_depth_target(image_index) ? std::optional<render_path_depth_target>{render_path_depth_target{
                                        .image = depth_images_.at(image_index).handle(),
                                        .image_view = *depth_image_views_.at(image_index),
                                        .old_layout = depth_layouts_.at(image_index),
                                        .format = depth_format_,
                                      }}
                                    : std::nullopt;

  build_render_path_frame_graph(frame_graph_, render_path_build_info{
                                                .path = settings_.path,
                                                .capabilities =
                                                    render_path_capabilities{
                                                      .bindless_descriptors = true,
                                                      .mesh_shader = optional_features.mesh_shader,
                                                      .task_shader = optional_features.task_shader,
                                                    },
                                                .swapchain =
                                                    render_path_swapchain_target{
                                                      .image = swapchain_images_.at(image_index),
                                                      .image_view = *swapchain_image_views_.at(image_index),
                                                      .old_layout = image_layouts_.at(image_index),
                                                      .extent = swapchain_extent_,
                                                      .clear_color = vk::ClearColorValue{settings_.clear_color},
                                                    },
                                                .depth = depth_target,
                                                .debug_draw = debug_draw,
                                              });
}

void renderer::record_frame_commands(frame_resources& frame, std::size_t image_index,
                                     const overlay_record_callback& overlay) {
  const vk::raii::CommandBuffer& command_buffer = frame.command_buffer;
  command_buffer.reset();
  update_debug_scene_uniforms(current_frame_);

  build_frame_graph(image_index);

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  command_buffer.begin(begin_info);
  frame_graph_.execute(command_buffer);

  image_layouts_.at(image_index) = vk::ImageLayout::ePresentSrcKHR;
  record_overlay_commands(command_buffer, image_index, overlay);

  command_buffer.end();

  image_layouts_.at(image_index) = vk::ImageLayout::ePresentSrcKHR;
  if (has_depth_target(image_index)) {
    depth_layouts_.at(image_index) = vk::ImageLayout::eDepthAttachmentOptimal;
  }
}

void renderer::record_overlay_commands(const vk::raii::CommandBuffer& command_buffer, std::size_t image_index,
                                       const overlay_record_callback& overlay) {
  if (!overlay) {
    return;
  }

  const vk::Image swapchain_image = swapchain_images_.at(image_index);

  const vk::ImageMemoryBarrier2 to_color_attachment = make_color_layout_barrier(
      swapchain_image, image_layouts_.at(image_index), vk::ImageLayout::eColorAttachmentOptimal,
      vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);

  vk::DependencyInfo to_color_attachment_dependency{};
  to_color_attachment_dependency.setImageMemoryBarriers(to_color_attachment);
  command_buffer.pipelineBarrier2(to_color_attachment_dependency);

  vk::RenderingAttachmentInfo color_attachment{};
  color_attachment.imageView = *swapchain_image_views_.at(image_index);
  color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
  color_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
  color_attachment.storeOp = vk::AttachmentStoreOp::eStore;

  vk::RenderingInfo rendering_info{};
  rendering_info.renderArea.offset = vk::Offset2D{0, 0};
  rendering_info.renderArea.extent = swapchain_extent_;
  rendering_info.layerCount = 1;
  rendering_info.setColorAttachments(color_attachment);

  command_buffer.beginRendering(rendering_info);
  overlay(static_cast<VkCommandBuffer>(*command_buffer));
  command_buffer.endRendering();

  const vk::ImageMemoryBarrier2 to_present = make_color_layout_barrier(
      swapchain_image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone);

  vk::DependencyInfo to_present_dependency{};
  to_present_dependency.setImageMemoryBarriers(to_present);
  command_buffer.pipelineBarrier2(to_present_dependency);

  image_layouts_.at(image_index) = vk::ImageLayout::ePresentSrcKHR;
}

void renderer::draw_frame_impl(const overlay_record_callback& overlay) {
  frame_resources& frame = frames_.at(current_frame_);
  if (!wait_for_timeline_value(frame.timeline_value)) {
    return;
  }

  const auto acquired_image =
      swapchain_.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *frame.image_available);
  const std::size_t image_index = acquired_image.second;

  if (!wait_for_timeline_value(image_timeline_values_.at(image_index))) {
    return;
  }

  record_frame_commands(frame, image_index, overlay);

  const std::uint64_t signal_timeline_value = next_timeline_value_++;
  const std::array wait_semaphores = {
    vk::SemaphoreSubmitInfo{
      *frame.image_available,
      0,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    },
  };
  const std::array command_buffers = {
    vk::CommandBufferSubmitInfo{
      *frame.command_buffer,
    },
  };
  const std::array signal_semaphores = {
    vk::SemaphoreSubmitInfo{
      *render_finished_.at(image_index),
      0,
      vk::PipelineStageFlagBits2::eAllCommands,
    },
    vk::SemaphoreSubmitInfo{
      *frame_timeline_,
      signal_timeline_value,
      vk::PipelineStageFlagBits2::eAllCommands,
    },
  };

  vk::SubmitInfo2 submit_info{};
  submit_info.setWaitSemaphoreInfos(wait_semaphores);
  submit_info.setCommandBufferInfos(command_buffers);
  submit_info.setSignalSemaphoreInfos(signal_semaphores);

  context_.device().graphics_queue().submit2(submit_info);
  frame.timeline_value = signal_timeline_value;
  image_timeline_values_.at(image_index) = signal_timeline_value;

  const std::array swapchains = {*swapchain_};
  const std::array image_indices = {acquired_image.second};
  const std::array present_wait_semaphores = {*render_finished_.at(image_index)};

  vk::PresentInfoKHR present_info{};
  present_info.setWaitSemaphores(present_wait_semaphores);
  present_info.setSwapchains(swapchains);
  present_info.setImageIndices(image_indices);

  const vk::Result present_result = context_.device().present_queue().presentKHR(present_info);
  ++frame_index_;
  current_frame_ = (current_frame_ + 1u) % frames_.size();

  const bool suboptimal =
      acquired_image.first == vk::Result::eSuboptimalKHR || present_result == vk::Result::eSuboptimalKHR;
  if (suboptimal && settings_.recreate_swapchain_on_suboptimal) {
    wait_device_idle();
    recreate_swapchain();
  }
}

void renderer::wait_device_idle() noexcept {
  try {
    context_.device().handle().waitIdle();
  } catch (const vk::Error& error) {
    RL_RENDER_ERROR("failed to wait for device idle: {}", error.what());
  }
}

}  // namespace rl::vulkan
