#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

namespace rl::vulkan {

enum class gpu_preference : std::uint8_t {
  automatic,
  discrete,
  integrated,
  first_suitable,
};

struct queue_family_indices {
  std::optional<std::uint32_t> graphics;
  std::optional<std::uint32_t> present;
  std::optional<std::uint32_t> compute;
  std::optional<std::uint32_t> transfer;

  [[nodiscard]] bool has_graphics_present() const noexcept { return graphics.has_value() && present.has_value(); }

  [[nodiscard]] std::vector<std::uint32_t> unique_families() const;
};

struct queue_family_info {
  std::uint32_t index = 0;
  vk::QueueFlags flags;
  std::uint32_t queue_count = 0;
  bool present_supported = false;
};

struct physical_device_feature_set {
  vk::PhysicalDeviceFeatures core{};
  vk::PhysicalDeviceVulkan11Features vulkan11{};
  vk::PhysicalDeviceVulkan12Features vulkan12{};
  vk::PhysicalDeviceVulkan13Features vulkan13{};
  vk::PhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer{};
  vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT graphics_pipeline_library{};
  vk::PhysicalDeviceMeshShaderFeaturesEXT mesh_shader{};
  vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesEXT present_mode_fifo_latest_ready{};
};

struct physical_device_limit_set {
  vk::PhysicalDeviceLimits core{};
  vk::PhysicalDeviceVulkan12Properties vulkan12{};
};

struct optional_device_features {
  bool descriptor_buffer = false;
  bool graphics_pipeline_library = false;
  bool mesh_shader = false;
  bool task_shader = false;
  bool memory_budget = false;
  bool scalar_block_layout = false;
  bool host_query_reset = false;
  bool multi_draw_indirect = false;
  bool present_mode_fifo_latest_ready = false;
};

struct device_requirements {
  bool require_graphics_queue = true;
  bool require_present_queue = true;
  bool require_swapchain = true;

  bool require_vulkan_1_3 = true;
  bool require_dynamic_rendering = true;
  bool require_synchronization2 = true;
  bool require_timeline_semaphore = true;
  bool require_sampler_anisotropy = true;
  bool require_shader_draw_parameters = true;
  bool require_descriptor_indexing = true;
  bool require_bindless_descriptors = true;
  bool require_buffer_device_address = true;

  std::vector<std::string> required_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };
};

struct physical_device_info {
  vk::PhysicalDevice handle{};

  vk::PhysicalDeviceProperties properties{};
  vk::PhysicalDeviceMemoryProperties memory_properties{};
  physical_device_feature_set features{};
  physical_device_limit_set limits{};
  optional_device_features optional_features{};

  std::vector<std::string> extensions;
  std::vector<std::string> enabled_extensions;
  std::vector<queue_family_info> queue_families;
  queue_family_indices selected_queues{};

  bool swapchain_adequate = false;
  bool suitable = false;
  int score = 0;

  std::vector<std::string> missing_requirements;
};

[[nodiscard]] std::string api_version_to_string(std::uint32_t version);
[[nodiscard]] std::string physical_device_name(const vk::PhysicalDeviceProperties& properties);
[[nodiscard]] std::string physical_device_type_to_string(vk::PhysicalDeviceType type);
[[nodiscard]] std::string queue_flags_to_string(vk::QueueFlags flags);

[[nodiscard]] std::uint64_t device_local_memory_bytes(const vk::PhysicalDeviceMemoryProperties& memory_properties);

[[nodiscard]] std::vector<physical_device_info> enumerate_physical_devices(const vk::raii::PhysicalDevices& devices,
                                                                           vk::SurfaceKHR surface,
                                                                           const device_requirements& requirements);

[[nodiscard]] std::optional<std::size_t> choose_physical_device(
    const std::vector<physical_device_info>& devices, gpu_preference preference,
    std::optional<std::size_t> preferred_index = std::nullopt);

}  // namespace rl::vulkan
