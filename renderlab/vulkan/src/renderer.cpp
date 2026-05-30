#include "vk/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "assets/file_io.hpp"
#include "base/log.hpp"
#include "vk/device.hpp"
#include "vk/frame_graph.hpp"
#include "vk/memory_allocator.hpp"
#include "vk/pipeline.hpp"
#include "vk/render_path.hpp"
#include "vk/vulkan_context.hpp"

namespace rl::vulkan {
namespace {

constexpr std::uint32_t min_frames_in_flight = 1;
constexpr std::uint32_t max_frames_in_flight = 3;
constexpr std::string_view triangle_vertex_shader_path = "renderlab/shaders/triangle.vert.spv";
constexpr std::string_view triangle_fragment_shader_path = "renderlab/shaders/triangle.frag.spv";

[[nodiscard]] bool extent_has_area(platform::extent2d extent) noexcept { return extent.width > 0 && extent.height > 0; }

[[nodiscard]] std::uint32_t clamp_frame_count(std::uint32_t frame_count) noexcept {
  return std::clamp(frame_count, min_frames_in_flight, max_frames_in_flight);
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
    : context_{context}, settings_{settings}, drawable_extent_{drawable_extent} {
  settings_.max_frames_in_flight = clamp_frame_count(settings_.max_frames_in_flight);
  create_command_pool();
  create_frame_resources();
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

void renderer::draw_frame() {
  if (!can_render()) {
    return;
  }

  try {
    draw_frame_impl();
  } catch (const vk::OutOfDateKHRError&) {
    wait_device_idle();
    recreate_swapchain();
  }
}

renderer_settings& renderer::settings() noexcept { return settings_; }

const renderer_settings& renderer::settings() const noexcept { return settings_; }

renderer_status renderer::status() const noexcept {
  return renderer_status{
    .drawable_extent = drawable_extent_,
    .swapchain_extent = swapchain_extent_,
    .swapchain_image_count = static_cast<std::uint32_t>(swapchain_images_.size()),
    .frame_index = frame_index_,
    .frame_graph_pass_count = static_cast<std::uint32_t>(frame_graph_.passes().size()),
    .path = settings_.path,
    .suspended = suspended_,
    .swapchain_ready = static_cast<bool>(*swapchain_) && has_drawable_extent(),
  };
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
  vk::CommandBufferAllocateInfo allocate_info{};
  allocate_info.commandPool = *command_pool_;
  allocate_info.level = vk::CommandBufferLevel::ePrimary;
  allocate_info.commandBufferCount = frame_count();

  std::vector<vk::raii::CommandBuffer> command_buffers =
      context_.device().handle().allocateCommandBuffers(allocate_info);

  const vk::SemaphoreCreateInfo semaphore_create_info{};
  vk::FenceCreateInfo fence_create_info{};
  fence_create_info.flags = vk::FenceCreateFlagBits::eSignaled;

  frames_.clear();
  frames_.reserve(command_buffers.size());

  for (vk::raii::CommandBuffer& command_buffer : command_buffers) {
    frames_.push_back(frame_resources{
      .command_buffer = std::move(command_buffer),
      .image_available = vk::raii::Semaphore{context_.device().handle(), semaphore_create_info},
      .in_flight = vk::raii::Fence{context_.device().handle(), fence_create_info},
    });
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
  const std::vector<vk::PresentModeKHR> present_modes = physical_device.getSurfacePresentModesKHR(surface);

  if (formats.empty() || present_modes.empty()) {
    throw std::runtime_error{"swapchain support is incomplete"};
  }

  surface_format_ = choose_surface_format(formats);
  present_mode_ = choose_present_mode(present_modes, settings_.preferred_present_mode);
  swapchain_extent_ = choose_swapchain_extent(capabilities, drawable_extent_);

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
  image_in_flight_fences_.assign(swapchain_images_.size(), nullptr);

  create_swapchain_image_views();
  create_depth_resources();
  create_debug_pipeline();

  const vk::SemaphoreCreateInfo semaphore_create_info{};
  render_finished_.reserve(swapchain_images_.size());
  for (std::size_t image_index = 0; image_index < swapchain_images_.size(); ++image_index) {
    render_finished_.emplace_back(context_.device().handle(), semaphore_create_info);
  }

  RL_RENDER_INFO("swapchain ready: {}x{}, images={}, frames_in_flight={}, present_mode={}", swapchain_extent_.width,
                 swapchain_extent_.height, swapchain_images_.size(), frames_.size(), vk::to_string(present_mode_));
}

void renderer::release_swapchain() noexcept {
  debug_triangle_pipeline_.reset();
  depth_image_views_.clear();
  depth_images_.clear();
  depth_layouts_.clear();
  depth_format_ = vk::Format::eUndefined;
  render_finished_.clear();
  swapchain_image_views_.clear();
  image_in_flight_fences_.clear();
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
    debug_vertex_shader_.emplace(context_.device().handle(), load_shader_bytes(triangle_vertex_shader_path));
  }

  if (!debug_fragment_shader_.has_value()) {
    debug_fragment_shader_.emplace(context_.device().handle(), load_shader_bytes(triangle_fragment_shader_path));
  }

  debug_triangle_pipeline_.emplace(context_.device().handle(), graphics_pipeline_description{
                                                                 .vertex_shader = std::cref(*debug_vertex_shader_),
                                                                 .fragment_shader = std::cref(*debug_fragment_shader_),
                                                                 .color_format = surface_format_.format,
                                                                 .depth_format = depth_format_,
                                                               });

  RL_SHADER_INFO("debug triangle pipeline ready: color_format={}", vk::to_string(surface_format_.format));
}

void renderer::build_frame_graph(std::size_t image_index) {
  const optional_device_features& optional_features = context_.device().optional_features();
  const std::optional<std::reference_wrapper<const graphics_pipeline>> debug_pipeline =
      debug_triangle_pipeline_.has_value()
          ? std::optional<std::reference_wrapper<const graphics_pipeline>>{std::cref(*debug_triangle_pipeline_)}
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
                                                      .clear_color = settings_.clear_color,
                                                    },
                                                .depth = depth_target,
                                                .debug_pipeline = debug_pipeline,
                                              });
}

void renderer::record_frame_commands(frame_resources& frame, std::size_t image_index) {
  const vk::raii::CommandBuffer& command_buffer = frame.command_buffer;
  command_buffer.reset();

  build_frame_graph(image_index);

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  command_buffer.begin(begin_info);
  frame_graph_.execute(command_buffer);
  command_buffer.end();

  image_layouts_.at(image_index) = vk::ImageLayout::ePresentSrcKHR;
  if (has_depth_target(image_index)) {
    depth_layouts_.at(image_index) = vk::ImageLayout::eDepthAttachmentOptimal;
  }
}

void renderer::draw_frame_impl() {
  frame_resources& frame = frames_.at(current_frame_);
  const std::array frame_fences = {*frame.in_flight};

  const vk::Result wait_result =
      context_.device().handle().waitForFences(frame_fences, vk::True, std::numeric_limits<std::uint64_t>::max());
  if (wait_result != vk::Result::eSuccess) {
    return;
  }

  const auto acquired_image =
      swapchain_.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *frame.image_available);
  const std::size_t image_index = acquired_image.second;

  const vk::Fence image_fence = image_in_flight_fences_.at(image_index);
  if (image_fence) {
    const std::array image_fences = {image_fence};
    const vk::Result image_fence_wait_result =
        context_.device().handle().waitForFences(image_fences, vk::True, std::numeric_limits<std::uint64_t>::max());
    if (image_fence_wait_result != vk::Result::eSuccess) {
      return;
    }
  }

  image_in_flight_fences_.at(image_index) = *frame.in_flight;

  context_.device().handle().resetFences(frame_fences);
  record_frame_commands(frame, image_index);

  const std::array wait_semaphores = {*frame.image_available};
  const std::array wait_stages = {vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput}};
  const std::array command_buffers = {*frame.command_buffer};
  const std::array signal_semaphores = {*render_finished_.at(image_index)};

  vk::SubmitInfo submit_info{};
  submit_info.setWaitSemaphores(wait_semaphores);
  submit_info.setWaitDstStageMask(wait_stages);
  submit_info.setCommandBuffers(command_buffers);
  submit_info.setSignalSemaphores(signal_semaphores);

  context_.device().graphics_queue().submit(submit_info, *frame.in_flight);

  const std::array swapchains = {*swapchain_};
  const std::array image_indices = {acquired_image.second};

  vk::PresentInfoKHR present_info{};
  present_info.setWaitSemaphores(signal_semaphores);
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
