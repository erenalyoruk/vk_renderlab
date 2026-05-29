#pragma once

#include <vulkan/vulkan.h>

#include "renderlab/base/noncopyable.hpp"
#include "renderlab/platform/sdl_window.hpp"

namespace rl::vulkan {

class vulkan_context final : public noncopyable {
 public:
  explicit vulkan_context(const platform::sdl_window& window);
  ~vulkan_context() noexcept;

  [[nodiscard]] VkInstance instance() const noexcept;
  [[nodiscard]] VkPhysicalDevice physical_device() const noexcept;
  [[nodiscard]] VkSurfaceKHR surface() const noexcept;

 private:
  void create_instance(const platform::sdl_window& window);
  void create_surface(const platform::sdl_window& window);
  void pick_physical_device();

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
};

}  // namespace rl::vulkan
