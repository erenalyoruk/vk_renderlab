#include "vk/clear_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "base/log.hpp"
#include "vk/device.hpp"
#include "vk/vulkan_context.hpp"

namespace rl::vulkan {
namespace {

[[nodiscard]] bool extent_has_area(platform::extent2d extent) noexcept { return extent.width > 0 && extent.height > 0; }

[[nodiscard]] vk::SurfaceFormatKHR choose_surface_format(const std::vector<vk::SurfaceFormatKHR>& formats) {
  const auto preferred = std::ranges::find_if(formats, [](const vk::SurfaceFormatKHR& format) {
    return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
  });

  if (preferred != formats.end()) {
    return *preferred;
  }

  return formats.at(0);
}

[[nodiscard]] vk::PresentModeKHR choose_present_mode(const std::vector<vk::PresentModeKHR>& present_modes) {
  if (std::ranges::contains(present_modes, vk::PresentModeKHR::eMailbox)) {
    return vk::PresentModeKHR::eMailbox;
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

[[nodiscard]] vk::ImageSubresourceRange color_subresource_range() noexcept {
  vk::ImageSubresourceRange result{};
  result.aspectMask = vk::ImageAspectFlagBits::eColor;
  result.baseMipLevel = 0;
  result.levelCount = 1;
  result.baseArrayLayer = 0;
  result.layerCount = 1;
  return result;
}

}  // namespace

clear_renderer::clear_renderer(const vulkan_context& context, platform::extent2d drawable_extent)
    : context_{context}, drawable_extent_{drawable_extent} {
  create_command_pool();
  create_sync_objects();
  recreate_swapchain();
}

clear_renderer::~clear_renderer() noexcept {
  try {
    context_.device().handle().waitIdle();
  } catch (const vk::Error& error) {
    RL_RENDER_ERROR("failed to wait for device idle during renderer shutdown: {}", error.what());
  }
}

void clear_renderer::resize(platform::extent2d drawable_extent) {
  if (drawable_extent.width == drawable_extent_.width && drawable_extent.height == drawable_extent_.height) {
    return;
  }

  drawable_extent_ = drawable_extent;
  context_.device().handle().waitIdle();
  recreate_swapchain();
}

void clear_renderer::draw_frame() {
  if (!has_drawable_extent()) {
    return;
  }

  try {
    draw_frame_impl();
  } catch (const vk::OutOfDateKHRError&) {
    context_.device().handle().waitIdle();
    recreate_swapchain();
  }
}

bool clear_renderer::has_drawable_extent() const noexcept { return extent_has_area(drawable_extent_); }

void clear_renderer::create_command_pool() {
  const auto graphics_queue = context_.device().queues().graphics;
  if (!graphics_queue.has_value()) {
    throw std::runtime_error{"clear renderer requires a graphics queue"};
  }

  vk::CommandPoolCreateInfo create_info{};
  create_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  create_info.queueFamilyIndex = *graphics_queue;

  command_pool_ = vk::raii::CommandPool{context_.device().handle(), create_info};
}

void clear_renderer::create_sync_objects() {
  const vk::SemaphoreCreateInfo semaphore_create_info{};
  image_available_ = vk::raii::Semaphore{context_.device().handle(), semaphore_create_info};

  vk::FenceCreateInfo fence_create_info{};
  fence_create_info.flags = vk::FenceCreateFlagBits::eSignaled;
  in_flight_ = vk::raii::Fence{context_.device().handle(), fence_create_info};
}

void clear_renderer::recreate_swapchain() {
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
  swapchain_extent_ = choose_swapchain_extent(capabilities, drawable_extent_);

  vk::SwapchainCreateInfoKHR create_info{};
  create_info.surface = surface;
  create_info.minImageCount = choose_image_count(capabilities);
  create_info.imageFormat = surface_format_.format;
  create_info.imageColorSpace = surface_format_.colorSpace;
  create_info.imageExtent = swapchain_extent_;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = vk::ImageUsageFlagBits::eTransferDst;
  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  create_info.presentMode = choose_present_mode(present_modes);
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

  const vk::SemaphoreCreateInfo semaphore_create_info{};
  render_finished_.reserve(swapchain_images_.size());
  for (std::size_t image_index = 0; image_index < swapchain_images_.size(); ++image_index) {
    render_finished_.emplace_back(context_.device().handle(), semaphore_create_info);
  }

  vk::CommandBufferAllocateInfo allocate_info{};
  allocate_info.commandPool = *command_pool_;
  allocate_info.level = vk::CommandBufferLevel::ePrimary;
  allocate_info.commandBufferCount = static_cast<std::uint32_t>(swapchain_images_.size());

  command_buffers_ = context_.device().handle().allocateCommandBuffers(allocate_info);

  RL_RENDER_INFO("swapchain ready: {}x{}, images={}", swapchain_extent_.width, swapchain_extent_.height,
                 swapchain_images_.size());
}

void clear_renderer::release_swapchain() noexcept {
  command_buffers_.clear();
  render_finished_.clear();
  image_layouts_.clear();
  swapchain_images_.clear();
  swapchain_ = nullptr;
}

void clear_renderer::record_clear_commands(std::size_t image_index) {
  const vk::raii::CommandBuffer& command_buffer = command_buffers_.at(image_index);
  command_buffer.reset();

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  command_buffer.begin(begin_info);

  const vk::Image image = swapchain_images_.at(image_index);
  const vk::ImageSubresourceRange subresource_range = color_subresource_range();

  vk::ImageMemoryBarrier2 to_transfer{};
  to_transfer.srcStageMask = vk::PipelineStageFlagBits2::eNone;
  to_transfer.srcAccessMask = vk::AccessFlagBits2::eNone;
  to_transfer.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
  to_transfer.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
  to_transfer.oldLayout = image_layouts_.at(image_index);
  to_transfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
  to_transfer.image = image;
  to_transfer.subresourceRange = subresource_range;

  vk::DependencyInfo to_transfer_dependency{};
  to_transfer_dependency.setImageMemoryBarriers(to_transfer);
  command_buffer.pipelineBarrier2(to_transfer_dependency);

  command_buffer.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, clear_color_, subresource_range);

  vk::ImageMemoryBarrier2 to_present{};
  to_present.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
  to_present.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
  to_present.dstStageMask = vk::PipelineStageFlagBits2::eNone;
  to_present.dstAccessMask = vk::AccessFlagBits2::eNone;
  to_present.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
  to_present.image = image;
  to_present.subresourceRange = subresource_range;

  vk::DependencyInfo to_present_dependency{};
  to_present_dependency.setImageMemoryBarriers(to_present);
  command_buffer.pipelineBarrier2(to_present_dependency);

  command_buffer.end();
  image_layouts_.at(image_index) = vk::ImageLayout::ePresentSrcKHR;
}

void clear_renderer::draw_frame_impl() {
  const std::array fences = {*in_flight_};
  const vk::Result wait_result =
      context_.device().handle().waitForFences(fences, vk::True, std::numeric_limits<std::uint64_t>::max());
  if (wait_result != vk::Result::eSuccess) {
    return;
  }

  const auto acquired_image = swapchain_.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *image_available_);

  context_.device().handle().resetFences(fences);

  const std::size_t image_index = acquired_image.second;
  record_clear_commands(image_index);

  const std::array wait_semaphores = {*image_available_};
  const std::array wait_stages = {vk::PipelineStageFlags{vk::PipelineStageFlagBits::eTransfer}};
  const std::array command_buffers = {*command_buffers_.at(image_index)};
  const std::array signal_semaphores = {*render_finished_.at(image_index)};

  vk::SubmitInfo submit_info{};
  submit_info.setWaitSemaphores(wait_semaphores);
  submit_info.setWaitDstStageMask(wait_stages);
  submit_info.setCommandBuffers(command_buffers);
  submit_info.setSignalSemaphores(signal_semaphores);

  context_.device().graphics_queue().submit(submit_info, *in_flight_);

  const std::array swapchains = {*swapchain_};
  const std::array image_indices = {acquired_image.second};

  vk::PresentInfoKHR present_info{};
  present_info.setWaitSemaphores(signal_semaphores);
  present_info.setSwapchains(swapchains);
  present_info.setImageIndices(image_indices);

  const vk::Result present_result = context_.device().present_queue().presentKHR(present_info);
  if (present_result == vk::Result::eSuboptimalKHR) {
    context_.device().handle().waitIdle();
    recreate_swapchain();
  }
}

}  // namespace rl::vulkan
