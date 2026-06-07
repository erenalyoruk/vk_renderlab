#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <vulkan/vulkan_raii.hpp>

#include "base/noncopyable.hpp"

namespace rl::vulkan {

enum class memory_usage : std::uint8_t {
  gpu_only,
  cpu_to_gpu,
  gpu_to_cpu,
};

struct memory_allocator_config {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  std::uint32_t vulkan_api_version = VK_API_VERSION_1_3;
  bool buffer_device_address = false;
};

struct buffer_description {
  vk::DeviceSize size = 0;
  vk::BufferUsageFlags usage;
  memory_usage memory = memory_usage::gpu_only;
  bool mapped = false;
};

struct image_description {
  vk::ImageType type = vk::ImageType::e2D;
  vk::Format format = vk::Format::eUndefined;
  vk::Extent3D extent{};
  vk::ImageUsageFlags usage;
  std::uint32_t mip_levels = 1;
  std::uint32_t array_layers = 1;
  vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
  memory_usage memory = memory_usage::gpu_only;
};

class gpu_buffer final {
 public:
  gpu_buffer() noexcept;
  ~gpu_buffer() noexcept = default;

  gpu_buffer(const gpu_buffer& other) = delete;
  gpu_buffer& operator=(const gpu_buffer& other) = delete;

  gpu_buffer(gpu_buffer&& other) noexcept = default;
  gpu_buffer& operator=(gpu_buffer&& other) noexcept = default;

  [[nodiscard]] vk::Buffer handle() const noexcept;
  [[nodiscard]] vk::DeviceSize size() const noexcept;
  [[nodiscard]] std::span<std::byte> mapped_bytes() noexcept;
  [[nodiscard]] std::span<const std::byte> mapped_bytes() const noexcept;
  void flush() const;
  [[nodiscard]] explicit operator bool() const noexcept;

 private:
  friend class memory_allocator;

  struct allocation_deleter {
    void* allocator = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;

    void operator()(void* allocation) const noexcept;
  };

  vk::Buffer buffer_{};
  vk::DeviceSize size_ = 0;
  std::span<std::byte> mapped_bytes_;
  std::unique_ptr<void, allocation_deleter> allocation_;
};

class gpu_image final {
 public:
  gpu_image() noexcept;
  ~gpu_image() noexcept = default;

  gpu_image(const gpu_image& other) = delete;
  gpu_image& operator=(const gpu_image& other) = delete;

  gpu_image(gpu_image&& other) noexcept = default;
  gpu_image& operator=(gpu_image&& other) noexcept = default;

  [[nodiscard]] vk::Image handle() const noexcept;
  [[nodiscard]] vk::Format format() const noexcept;
  [[nodiscard]] vk::Extent3D extent() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept;

 private:
  friend class memory_allocator;

  struct allocation_deleter {
    void* allocator = nullptr;
    VkImage image = VK_NULL_HANDLE;

    void operator()(void* allocation) const noexcept;
  };

  vk::Image image_{};
  vk::Format format_ = vk::Format::eUndefined;
  vk::Extent3D extent_{};
  std::unique_ptr<void, allocation_deleter> allocation_;
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

  [[nodiscard]] gpu_buffer create_buffer(const buffer_description& description) const;
  [[nodiscard]] gpu_image create_image(const image_description& description) const;

 private:
  struct allocator_deleter {
    void operator()(void* allocator) const noexcept;
  };

  [[nodiscard]] void* native_handle() const noexcept;

  std::unique_ptr<void, allocator_deleter> allocator_;
};

}  // namespace rl::vulkan
