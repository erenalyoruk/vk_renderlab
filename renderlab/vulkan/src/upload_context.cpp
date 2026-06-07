#include "vk/upload_context.hpp"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "vk/device.hpp"
#include "vk/vulkan_context.hpp"

namespace rl::vulkan {

namespace {

void write_staging_bytes(gpu_buffer& buffer, std::span<const std::byte> bytes) {
  const std::span<std::byte> mapped_bytes = buffer.mapped_bytes();
  if (mapped_bytes.size_bytes() < bytes.size_bytes()) {
    throw std::runtime_error{"mapped staging buffer is too small for upload"};
  }

  std::memcpy(mapped_bytes.data(), bytes.data(), bytes.size_bytes());
  buffer.flush();
}

}  // namespace

upload_context::upload_context(const vulkan_context& context) : context_{&context} {
  const auto graphics_queue = context.device().queues().graphics;
  if (!graphics_queue.has_value()) {
    throw std::runtime_error{"upload context requires a graphics queue"};
  }

  vk::CommandPoolCreateInfo create_info{};
  create_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  create_info.queueFamilyIndex = *graphics_queue;

  command_pool_ = vk::raii::CommandPool{context.device().handle(), create_info};
}

gpu_buffer upload_context::create_device_buffer(std::span<const std::byte> bytes, vk::BufferUsageFlags usage) const {
  if (context_ == nullptr) {
    throw std::runtime_error{"upload context is not initialized"};
  }

  gpu_buffer staging_buffer = context_->allocator().create_buffer(buffer_description{
    .size = bytes.size_bytes(),
    .usage = vk::BufferUsageFlagBits::eTransferSrc,
    .memory = memory_usage::cpu_to_gpu,
    .mapped = true,
  });
  gpu_buffer device_buffer = context_->allocator().create_buffer(buffer_description{
    .size = staging_buffer.size(),
    .usage = vk::BufferUsageFlagBits::eTransferDst | usage,
    .memory = memory_usage::gpu_only,
  });

  write_staging_bytes(staging_buffer, bytes);
  copy_buffer(staging_buffer.handle(), device_buffer.handle(), staging_buffer.size());
  return device_buffer;
}

void upload_context::copy_buffer(vk::Buffer source, vk::Buffer destination, vk::DeviceSize size) const {
  if (context_ == nullptr) {
    throw std::runtime_error{"upload context is not initialized"};
  }

  vk::CommandBufferAllocateInfo allocate_info{};
  allocate_info.commandPool = *command_pool_;
  allocate_info.level = vk::CommandBufferLevel::ePrimary;
  allocate_info.commandBufferCount = 1;

  std::vector<vk::raii::CommandBuffer> command_buffers =
      context_->device().handle().allocateCommandBuffers(allocate_info);
  const vk::raii::CommandBuffer& command_buffer = command_buffers.at(0);

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  command_buffer.begin(begin_info);
  vk::BufferCopy copy_region{};
  copy_region.srcOffset = 0;
  copy_region.dstOffset = 0;
  copy_region.size = size;
  command_buffer.copyBuffer(source, destination, copy_region);
  command_buffer.end();

  vk::CommandBufferSubmitInfo command_buffer_info{};
  command_buffer_info.commandBuffer = *command_buffer;
  vk::SubmitInfo2 submit_info{};
  submit_info.setCommandBufferInfos(command_buffer_info);

  context_->device().graphics_queue().submit2(submit_info);
  context_->device().graphics_queue().waitIdle();
}

}  // namespace rl::vulkan
