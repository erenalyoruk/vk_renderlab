#include "vk/memory_allocator.hpp"

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
  if (result != VK_SUCCESS) {
    throw std::runtime_error{"failed to create VMA allocator: VkResult " + std::to_string(result)};
  }

  allocator_.reset(raw_allocator);
  RL_VK_INFO("Vulkan Memory Allocator initialized");
}

void memory_allocator::allocator_deleter::operator()(void* allocator) const noexcept {
  if (allocator == nullptr) {
    return;
  }

  vmaDestroyAllocator(static_cast<VmaAllocator>(allocator));
}

}  // namespace rl::vulkan
