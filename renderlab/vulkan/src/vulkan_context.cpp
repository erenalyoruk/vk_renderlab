#include "vk/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "base/log.hpp"
#include "vk/vk_check.hpp"

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

[[nodiscard]] std::string vk_api_version_string(std::uint32_t version) {
  return std::to_string(VK_VERSION_MAJOR(version)) + "." + std::to_string(VK_VERSION_MINOR(version)) + "." +
         std::to_string(VK_VERSION_PATCH(version));
}

[[nodiscard]] std::string_view physical_device_type_name(VkPhysicalDeviceType type) noexcept {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
      return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return "integrated_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return "discrete_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return "virtual_gpu";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return "cpu";
    default:
      return "unknown";
  }
}

[[nodiscard]] std::string optional_queue_family_text(const std::optional<std::uint32_t>& family) {
  if (!family.has_value()) {
    return "none";
  }

  return std::to_string(*family);
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
  RL_VK_INFO("initializing Vulkan context");

  create_instance(window);
  create_surface(window);
  pick_physical_device();

  RL_VK_INFO("Vulkan context initialized");
}

vulkan_context::~vulkan_context() noexcept {
  if (surface_ != VK_NULL_HANDLE) {
    RL_VK_DEBUG("destroying Vulkan surface");
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }

  if (instance_ != VK_NULL_HANDLE) {
    RL_VK_DEBUG("destroying Vulkan instance");
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

VkInstance vulkan_context::instance() const noexcept { return instance_; }

VkPhysicalDevice vulkan_context::physical_device() const noexcept { return physical_device_; }

VkSurfaceKHR vulkan_context::surface() const noexcept { return surface_; }

void vulkan_context::create_instance([[maybe_unused]] const platform::sdl_window& window) {
  RL_VK_INFO("creating Vulkan instance");

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Vulkan RenderLab";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.pEngineName = "RenderLab";
  app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);

  // Keep this at 1.3 for the first bootstrap. In the next Vulkan patch we will query
  // instance/device support and enable modern features deliberately.
  app_info.apiVersion = VK_API_VERSION_1_3;

  std::vector<const char*> extensions = platform::sdl_window::required_vulkan_extensions();

  RL_VK_DEBUG("requested Vulkan API version: {}", vk_api_version_string(app_info.apiVersion));

  for (std::size_t index = 0; index < extensions.size(); ++index) {
    RL_VK_TRACE("instance extension[{}]: {}", index, extensions[index]);
  }

  std::vector<const char*> enabled_layers;
  if (want_validation_layers() && validation_layers_available()) {
    enabled_layers.assign(validation_layers.begin(), validation_layers.end());
    RL_VK_INFO("Vulkan validation layers enabled");
  } else if (want_validation_layers()) {
    RL_VK_WARN("Vulkan validation layer requested but VK_LAYER_KHRONOS_validation was not found");
  }

  for (std::size_t index = 0; index < enabled_layers.size(); ++index) {
    RL_VK_TRACE("instance layer[{}]: {}", index, enabled_layers[index]);
  }

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();
  create_info.enabledLayerCount = static_cast<std::uint32_t>(enabled_layers.size());
  create_info.ppEnabledLayerNames = enabled_layers.data();

  check_vk(vkCreateInstance(&create_info, nullptr, &instance_), "vkCreateInstance");

  RL_VK_INFO("Vulkan instance created");
}

void vulkan_context::create_surface(const platform::sdl_window& window) {
  surface_ = window.create_vulkan_surface(instance_);
  RL_VK_INFO("Vulkan presentation surface created");
}

void vulkan_context::pick_physical_device() {
  RL_GPU_INFO("enumerating Vulkan physical devices");

  std::uint32_t device_count = 0;
  check_vk(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr), "vkEnumeratePhysicalDevices");

  if (device_count == 0) {
    throw std::runtime_error("no Vulkan-capable physical device found");
  }

  std::vector<VkPhysicalDevice> devices(device_count);
  check_vk(vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()), "vkEnumeratePhysicalDevices");

  RL_GPU_INFO("found {} Vulkan physical device(s)", device_count);

  for (std::size_t index = 0; index < devices.size(); ++index) {
    VkPhysicalDevice device = devices[index];

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);

    const queue_family_indices queue_families = find_queue_families(device, surface_);
    const bool suitable = is_device_suitable(device, surface_);

    RL_GPU_INFO("GPU[{}]: '{}' type={} vendor=0x{:04x} device=0x{:04x} api={} suitable={}", index,
                properties.deviceName, physical_device_type_name(properties.deviceType), properties.vendorID,
                properties.deviceID, vk_api_version_string(properties.apiVersion), suitable);

    RL_GPU_DEBUG("GPU[{}] queue families: graphics={}, present={}", index,
                 optional_queue_family_text(queue_families.graphics),
                 optional_queue_family_text(queue_families.present));

    if (!suitable) {
      continue;
    }

    physical_device_ = device;
    RL_GPU_INFO("selected Vulkan physical device: '{}'", properties.deviceName);
    return;
  }

  throw std::runtime_error("no graphics/present-capable Vulkan physical device found");
}
}  // namespace rl::vulkan
