#pragma once

#include <optional>

#include <vulkan/vulkan_raii.hpp>

#include "base/noncopyable.hpp"
#include "vk/physical_device.hpp"

namespace rl::vulkan {

class device final : public noncopyable {
 public:
  device() = default;
  device(const vk::raii::PhysicalDevice& physical_device_handle, const physical_device_info& physical_device,
         const device_requirements& requirements);
  ~device() noexcept;

  device(device& other) = delete;
  device& operator=(device& other) = delete;

  device(device&& other) noexcept = delete;
  device& operator=(device&& other) noexcept = delete;

  [[nodiscard]] const vk::raii::Device& handle() const noexcept;
  [[nodiscard]] vk::Device raw_handle() const noexcept;
  [[nodiscard]] VkDevice c_handle() const noexcept;

  [[nodiscard]] const vk::raii::Queue& graphics_queue() const noexcept;
  [[nodiscard]] const vk::raii::Queue& present_queue() const noexcept;
  [[nodiscard]] const std::optional<vk::raii::Queue>& compute_queue() const noexcept;
  [[nodiscard]] const std::optional<vk::raii::Queue>& transfer_queue() const noexcept;

  [[nodiscard]] VkQueue c_graphics_queue() const noexcept;
  [[nodiscard]] VkQueue c_present_queue() const noexcept;
  [[nodiscard]] VkQueue c_compute_queue() const noexcept;
  [[nodiscard]] VkQueue c_transfer_queue() const noexcept;

  [[nodiscard]] const queue_family_indices& queues() const noexcept;
  [[nodiscard]] const optional_device_features& optional_features() const noexcept;

 private:
  vk::raii::Device handle_{nullptr};
  vk::raii::Queue graphics_queue_{nullptr};
  vk::raii::Queue present_queue_{nullptr};
  std::optional<vk::raii::Queue> compute_queue_;
  std::optional<vk::raii::Queue> transfer_queue_;
  queue_family_indices queues_{};
  optional_device_features optional_features_{};
};

}  // namespace rl::vulkan
