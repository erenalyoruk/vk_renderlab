#pragma once

#include <cstddef>
#include <span>

#include <vulkan/vulkan_raii.hpp>

#include "base/noncopyable.hpp"
#include "vk/memory_allocator.hpp"

namespace rl::vulkan {

class vulkan_context;

class upload_context final : public noncopyable {
 public:
  upload_context() = default;
  explicit upload_context(const vulkan_context& context);
  ~upload_context() noexcept = default;

  upload_context(upload_context& other) = delete;
  upload_context& operator=(upload_context& other) = delete;

  upload_context(upload_context&& other) noexcept = delete;
  upload_context& operator=(upload_context&& other) noexcept = delete;

  [[nodiscard]] gpu_buffer create_device_buffer(std::span<const std::byte> bytes, vk::BufferUsageFlags usage) const;
  void copy_buffer(vk::Buffer source, vk::Buffer destination, vk::DeviceSize size) const;

 private:
  const vulkan_context* context_ = nullptr;
  vk::raii::CommandPool command_pool_{nullptr};
};

}  // namespace rl::vulkan
