#pragma once

#include <cstdint>
#include <memory>

#include <vulkan/vulkan_core.h>

#include "base/noncopyable.hpp"

namespace rl::vulkan {

struct memory_allocator_config {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  std::uint32_t vulkan_api_version = VK_API_VERSION_1_3;
  bool buffer_device_address = false;
};

class memory_allocator final : public noncopyable {
 public:
  memory_allocator() = default;
  explicit memory_allocator(const memory_allocator_config& config);
  ~memory_allocator() noexcept = default;

  memory_allocator(memory_allocator& other) = delete;
  memory_allocator& operator=(memory_allocator& other) = delete;

  memory_allocator(memory_allocator&& other) noexcept = delete;
  memory_allocator& operator=(memory_allocator&& other) noexcept = delete;

 private:
  struct allocator_deleter {
    void operator()(void* allocator) const noexcept;
  };

  std::unique_ptr<void, allocator_deleter> allocator_;
};

}  // namespace rl::vulkan
