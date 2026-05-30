#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "base/noncopyable.hpp"
#include "platform/sdl_window.hpp"
#include "vk/device.hpp"
#include "vk/memory_allocator.hpp"
#include "vk/physical_device.hpp"

namespace rl::vulkan {

struct vulkan_context_config {
#ifndef NDEBUG
  bool enable_validation = true;
#else
  bool enable_validation = false;
#endif

  gpu_preference preference = gpu_preference::automatic;
  std::optional<std::size_t> preferred_device_index = std::nullopt;

  device_requirements requirements{};
};

class vulkan_context final : public noncopyable {
 public:
  explicit vulkan_context(const platform::sdl_window& window, vulkan_context_config config = {});
  ~vulkan_context() noexcept;

  vulkan_context(vulkan_context& other) = delete;
  vulkan_context& operator=(vulkan_context& other) = delete;

  vulkan_context(vulkan_context&& other) noexcept = delete;
  vulkan_context& operator=(vulkan_context&& other) noexcept = delete;

  [[nodiscard]] const vk::raii::Context& context() const noexcept;
  [[nodiscard]] const vk::raii::Instance& instance() const noexcept;
  [[nodiscard]] const vk::raii::SurfaceKHR& surface() const noexcept;
  [[nodiscard]] const rl::vulkan::device& device() const noexcept;
  [[nodiscard]] const memory_allocator& allocator() const noexcept;

  [[nodiscard]] vk::Instance raw_instance() const noexcept;
  [[nodiscard]] vk::SurfaceKHR raw_surface() const noexcept;
  [[nodiscard]] vk::PhysicalDevice physical_device() const;
  [[nodiscard]] vk::PhysicalDevice raw_physical_device() const;
  [[nodiscard]] vk::Device raw_device() const noexcept;

  [[nodiscard]] VkInstance c_instance() const noexcept;
  [[nodiscard]] VkSurfaceKHR c_surface() const noexcept;
  [[nodiscard]] VkPhysicalDevice c_physical_device() const;
  [[nodiscard]] VkDevice c_device() const noexcept;

  [[nodiscard]] const std::vector<physical_device_info>& physical_devices() const noexcept;
  [[nodiscard]] const physical_device_info& selected_physical_device() const;
  [[nodiscard]] const vk::raii::PhysicalDevice& selected_physical_device_handle() const;
  [[nodiscard]] std::size_t selected_physical_device_index() const noexcept;

 private:
  vulkan_context_config config_{};

  bool validation_enabled_ = false;
  bool debug_utils_enabled_ = false;

  vk::raii::Context context_;
  vk::raii::Instance instance_{nullptr};
  vk::raii::DebugUtilsMessengerEXT debug_messenger_{nullptr};
  vk::raii::SurfaceKHR surface_{nullptr};

  std::optional<vk::raii::PhysicalDevices> physical_device_handles_;
  std::vector<physical_device_info> physical_devices_;
  std::size_t selected_physical_device_index_ = 0;

  std::optional<rl::vulkan::device> device_;
  std::optional<memory_allocator> allocator_;

  void create_instance(const platform::sdl_window& window);
  void create_debug_messenger();
  void create_surface(const platform::sdl_window& window);
  void enumerate_and_select_physical_device();
  void create_logical_device();
  void create_memory_allocator();

  [[nodiscard]] bool validation_layers_available() const;
  [[nodiscard]] bool instance_extension_available(std::string_view extension_name) const;
};

}  // namespace rl::vulkan
