#include "renderlab/vulkan/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "renderlab/vulkan/vk_check.hpp"

namespace rl::vulkan {
namespace {
constexpr std::array validation_layers = {"VK_LAYER_KHRONOS_validation"};

[[nodiscard]] bool want_validation_layers() noexcept {
#ifndef NDEBUG
  return true;
#else
  return false;
#endif
}

[[nodiscard]] bool validation_layers_available() {
  std::uint32_t layer_count = 0;
  check_vk(vkEnumerateInstanceLayerProperties(&layer_count, nullptr), "vkEnumerateInstanceLayerProperties");

  std::vector<VkLayerProperties> available_layers(layer_count);
  check_vk(vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data()),
           "vkEnumerateInstanceLayerProperties");

  return std::ranges::all_of(validation_layers, [&](const char* required_layer) {
    return std::ranges::any_of(available_layers, [&](const VkLayerProperties& layer) {
      return std::string(static_cast<const char*>(layer.layerName)) == required_layer;
    });
  });
}

struct queue_family_indices {
  std::optional<std::uint32_t> graphics;
  std::optional<std::uint32_t> present;

  [[nodiscard]] bool complete() const noexcept { return graphics.has_value() && present.has_value(); }
};

[[nodiscard]] queue_family_indices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
  std::uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

  queue_family_indices indices{};
  for (std::uint32_t index = 0; index < queue_family_count; ++index) {
    const VkQueueFamilyProperties& queue_family = queue_families[index];
    if ((queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
      indices.graphics = index;
    }

    VkBool32 present_supported = VK_FALSE;
    check_vk(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present_supported),
             "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (present_supported == VK_TRUE) {
      indices.present = index;
    }

    if (indices.complete()) {
      break;
    }
  }

  return indices;
}

[[nodiscard]] bool is_device_suitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
  return find_queue_families(device, surface).complete();
}
}  // namespace

vulkan_context::vulkan_context(const platform::sdl_window& window) {
  create_instance(window);
  create_surface(window);
  pick_physical_device();
}

vulkan_context::~vulkan_context() noexcept {
  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }

  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

VkInstance vulkan_context::instance() const noexcept { return instance_; }

VkPhysicalDevice vulkan_context::physical_device() const noexcept { return physical_device_; }

VkSurfaceKHR vulkan_context::surface() const noexcept { return surface_; }

void vulkan_context::create_instance(const platform::sdl_window& window) {
  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Vulkan RenderLab";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.pEngineName = "RenderLab";
  app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.apiVersion = VK_API_VERSION_1_3;

  std::vector<const char*> extensions = window.required_vulkan_extensions();

  std::vector<const char*> enabled_layers;
  if (want_validation_layers() && validation_layers_available()) {
    enabled_layers.assign(validation_layers.begin(), validation_layers.end());
    spdlog::info("Vulkan validation layers enabled");
  } else if (want_validation_layers()) {
    spdlog::warn("Vulkan validation layer requested but VK_LAYER_KHRONOS_validation was not found");
  }

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();
  create_info.enabledLayerCount = static_cast<std::uint32_t>(enabled_layers.size());
  create_info.ppEnabledLayerNames = enabled_layers.data();

  check_vk(vkCreateInstance(&create_info, nullptr, &instance_), "vkCreateInstance");
}

void vulkan_context::create_surface(const platform::sdl_window& window) {
  surface_ = window.create_vulkan_surface(instance_);
}

void vulkan_context::pick_physical_device() {
  std::uint32_t device_count = 0;
  check_vk(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr), "vkEnumeratePhysicalDevices");

  if (device_count == 0) {
    throw std::runtime_error("no Vulkan-capable physical device found");
  }

  std::vector<VkPhysicalDevice> devices(device_count);
  check_vk(vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()), "vkEnumeratePhysicalDevices");

  for (VkPhysicalDevice device : devices) {
    if (!is_device_suitable(device, surface_)) {
      continue;
    }

    physical_device_ = device;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device_, &properties);
    spdlog::info("selected Vulkan physical device: {}", properties.deviceName);
    return;
  }

  throw std::runtime_error("no graphics/present-capable Vulkan physical device found");
}
}  // namespace rl::vulkan
