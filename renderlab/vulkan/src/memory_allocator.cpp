#include "vk/memory_allocator.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION
#define VMA_NULLABLE
#define VMA_NOT_NULL
#define VMA_VULKAN_VERSION 1003000

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wmissing-field-initializers"
  #pragma clang diagnostic ignored "-Wunused-parameter"
  #pragma clang diagnostic ignored "-Wunused-private-field"
  #pragma clang diagnostic ignored "-Wunused-variable"
#endif

#include <vk_mem_alloc.h>

#ifdef __clang__
  #pragma clang diagnostic pop
#endif

#include "base/log.hpp"

namespace rl::vulkan {
namespace {

[[nodiscard]] VmaMemoryUsage to_vma_memory_usage(memory_usage usage) noexcept {
  switch (usage) {
    case memory_usage::gpu_only:
      return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    case memory_usage::cpu_to_gpu:
    case memory_usage::gpu_to_cpu:
      return VMA_MEMORY_USAGE_AUTO;
  }

  return VMA_MEMORY_USAGE_AUTO;
}

[[nodiscard]] VmaAllocationCreateFlags to_vma_allocation_flags(const buffer_description& description) noexcept {
  VmaAllocationCreateFlags flags = {};

  if (description.memory == memory_usage::cpu_to_gpu) {
    flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  }

  if (description.memory == memory_usage::gpu_to_cpu) {
    flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  }

  if (description.mapped) {
    flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
  }

  return flags;
}

[[nodiscard]] VmaAllocationCreateFlags to_vma_allocation_flags(const image_description& description) noexcept {
  VmaAllocationCreateFlags flags = {};

  if (description.memory == memory_usage::cpu_to_gpu) {
    flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  }

  if (description.memory == memory_usage::gpu_to_cpu) {
    flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  }

  return flags;
}

[[nodiscard]] VkBufferCreateInfo make_buffer_create_info(const buffer_description& description) noexcept {
  VkBufferCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  create_info.size = static_cast<VkDeviceSize>(description.size);
  create_info.usage = static_cast<VkBufferUsageFlags>(description.usage);
  create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  return create_info;
}

[[nodiscard]] VkImageCreateInfo make_image_create_info(const image_description& description) noexcept {
  return VkImageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .imageType = static_cast<VkImageType>(description.type),
    .format = static_cast<VkFormat>(description.format),
    .extent =
        VkExtent3D{
          .width = description.extent.width,
          .height = description.extent.height,
          .depth = description.extent.depth,
        },
    .mipLevels = description.mip_levels,
    .arrayLayers = description.array_layers,
    .samples = static_cast<VkSampleCountFlagBits>(description.samples),
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = static_cast<VkImageUsageFlags>(description.usage),
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = nullptr,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
}

[[nodiscard]] VmaAllocationCreateInfo make_allocation_create_info(const buffer_description& description) noexcept {
  VmaAllocationCreateInfo create_info{};
  create_info.usage = to_vma_memory_usage(description.memory);
  create_info.flags = to_vma_allocation_flags(description);
  return create_info;
}

[[nodiscard]] VmaAllocationCreateInfo make_allocation_create_info(const image_description& description) noexcept {
  VmaAllocationCreateInfo create_info{};
  create_info.usage = to_vma_memory_usage(description.memory);
  create_info.flags = to_vma_allocation_flags(description);
  return create_info;
}

void check_vma_result(VkResult result, std::string_view operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error{std::string{operation} + " failed: VkResult " + std::to_string(result)};
  }
}

}  // namespace

gpu_buffer::gpu_buffer() noexcept : allocation_{nullptr, allocation_deleter{}} {}

vk::Buffer gpu_buffer::handle() const noexcept { return buffer_; }

vk::DeviceSize gpu_buffer::size() const noexcept { return size_; }

std::span<std::byte> gpu_buffer::mapped_bytes() noexcept { return mapped_bytes_; }

std::span<const std::byte> gpu_buffer::mapped_bytes() const noexcept { return mapped_bytes_; }

void gpu_buffer::flush() const {
  if (allocation_ == nullptr || mapped_bytes_.empty()) {
    return;
  }

  const VkResult result = vmaFlushAllocation(static_cast<VmaAllocator>(allocation_.get_deleter().allocator),
                                             static_cast<VmaAllocation>(allocation_.get()), 0, VK_WHOLE_SIZE);
  check_vma_result(result, "vmaFlushAllocation");
}

gpu_buffer::operator bool() const noexcept { return static_cast<bool>(buffer_); }

void gpu_buffer::allocation_deleter::operator()(void* allocation) const noexcept {
  if (allocator == nullptr || allocation == nullptr || buffer == VK_NULL_HANDLE) {
    return;
  }

  vmaDestroyBuffer(static_cast<VmaAllocator>(allocator), buffer, static_cast<VmaAllocation>(allocation));
}

gpu_image::gpu_image() noexcept : allocation_{nullptr, allocation_deleter{}} {}

vk::Image gpu_image::handle() const noexcept { return image_; }

vk::Format gpu_image::format() const noexcept { return format_; }

vk::Extent3D gpu_image::extent() const noexcept { return extent_; }

gpu_image::operator bool() const noexcept { return static_cast<bool>(image_); }

void gpu_image::allocation_deleter::operator()(void* allocation) const noexcept {
  if (allocator == nullptr || allocation == nullptr || image == VK_NULL_HANDLE) {
    return;
  }

  vmaDestroyImage(static_cast<VmaAllocator>(allocator), image, static_cast<VmaAllocation>(allocation));
}

memory_allocator::memory_allocator(const memory_allocator_config& config) {
  VmaAllocatorCreateInfo create_info{};
  create_info.instance = config.instance;
  create_info.physicalDevice = config.physical_device;
  create_info.device = config.device;
  create_info.vulkanApiVersion = config.vulkan_api_version;

  if (config.buffer_device_address) {
    create_info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  }

  VmaAllocator raw_allocator = nullptr;
  const VkResult result = vmaCreateAllocator(&create_info, &raw_allocator);
  check_vma_result(result, "vmaCreateAllocator");

  allocator_.reset(raw_allocator);
  RL_VK_INFO("Vulkan Memory Allocator initialized");
}

gpu_buffer memory_allocator::create_buffer(const buffer_description& description) const {
  if (description.size == 0) {
    throw std::runtime_error{"buffer size must be greater than zero"};
  }

  VkBuffer raw_buffer = VK_NULL_HANDLE;
  VmaAllocation raw_allocation = nullptr;
  VmaAllocationInfo allocation_info{};

  const VkBufferCreateInfo buffer_create_info = make_buffer_create_info(description);
  const VmaAllocationCreateInfo allocation_create_info = make_allocation_create_info(description);

  const VkResult result = vmaCreateBuffer(static_cast<VmaAllocator>(native_handle()), &buffer_create_info,
                                          &allocation_create_info, &raw_buffer, &raw_allocation, &allocation_info);
  check_vma_result(result, "vmaCreateBuffer");

  gpu_buffer buffer;
  buffer.buffer_ = vk::Buffer{raw_buffer};
  buffer.size_ = description.size;
  buffer.allocation_ = std::unique_ptr<void, gpu_buffer::allocation_deleter>{
    raw_allocation,
    gpu_buffer::allocation_deleter{
      .allocator = native_handle(),
      .buffer = raw_buffer,
    },
  };

  if (allocation_info.pMappedData != nullptr) {
    buffer.mapped_bytes_ =
        std::span{static_cast<std::byte*>(allocation_info.pMappedData), static_cast<std::size_t>(description.size)};
  }

  return buffer;
}

gpu_image memory_allocator::create_image(const image_description& description) const {
  if (description.format == vk::Format::eUndefined) {
    throw std::runtime_error{"image format must not be undefined"};
  }

  if (description.extent.width == 0 || description.extent.height == 0 || description.extent.depth == 0) {
    throw std::runtime_error{"image extent must be greater than zero"};
  }

  VkImage raw_image = VK_NULL_HANDLE;
  VmaAllocation raw_allocation = nullptr;

  const VkImageCreateInfo image_create_info = make_image_create_info(description);
  const VmaAllocationCreateInfo allocation_create_info = make_allocation_create_info(description);

  const VkResult result = vmaCreateImage(static_cast<VmaAllocator>(native_handle()), &image_create_info,
                                         &allocation_create_info, &raw_image, &raw_allocation, nullptr);
  check_vma_result(result, "vmaCreateImage");

  gpu_image image;
  image.image_ = vk::Image{raw_image};
  image.format_ = description.format;
  image.extent_ = description.extent;
  image.allocation_ = std::unique_ptr<void, gpu_image::allocation_deleter>{
    raw_allocation,
    gpu_image::allocation_deleter{
      .allocator = native_handle(),
      .image = raw_image,
    },
  };

  return image;
}

void memory_allocator::allocator_deleter::operator()(void* allocator) const noexcept {
  if (allocator == nullptr) {
    return;
  }

  vmaDestroyAllocator(static_cast<VmaAllocator>(allocator));
}

void* memory_allocator::native_handle() const noexcept { return allocator_.get(); }

}  // namespace rl::vulkan
