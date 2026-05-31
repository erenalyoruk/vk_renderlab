#include "vk/physical_device.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace rl::vulkan {
namespace {

template <typename FixedString>
[[nodiscard]] std::string copy_fixed_c_string(const FixedString& value) {
  if constexpr (requires { value.data(); }) {
    return std::string{value.data()};
  } else {
    return std::string{value};
  }
}

[[nodiscard]] bool has_queue_flag(const queue_family_info& family, vk::QueueFlagBits flag) noexcept {
  return static_cast<bool>(family.flags & flag);
}

[[nodiscard]] bool contains_extension(const std::vector<std::string>& extensions, std::string_view required_extension) {
  return std::ranges::any_of(extensions, [&](const std::string& extension) { return extension == required_extension; });
}

void append_enabled_extension(physical_device_info& info, std::string_view extension) {
  if (!contains_extension(info.enabled_extensions, extension)) {
    info.enabled_extensions.emplace_back(extension);
  }
}

void append_extension_with_dependencies(physical_device_info& info, std::string_view extension) {
  if (extension == VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME) {
    append_enabled_extension(info, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
  }

  append_enabled_extension(info, extension);
}

[[nodiscard]] std::vector<std::string> enumerate_device_extensions(const vk::raii::PhysicalDevice& device) {
  const std::vector<vk::ExtensionProperties> properties = device.enumerateDeviceExtensionProperties();

  std::vector<std::string> extensions;
  extensions.reserve(properties.size());

  for (const vk::ExtensionProperties& extension : properties) {
    extensions.emplace_back(copy_fixed_c_string(extension.extensionName));
  }

  return extensions;
}

[[nodiscard]] physical_device_feature_set query_features(const vk::raii::PhysicalDevice& device) {
  auto feature_chain =
      device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                          vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,
                          vk::PhysicalDeviceDescriptorBufferFeaturesEXT,
                          vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT,
                          vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesEXT>();

  auto& features2 = feature_chain.get<vk::PhysicalDeviceFeatures2>();

  physical_device_feature_set result{};
  result.core = features2.features;
  result.vulkan11 = feature_chain.get<vk::PhysicalDeviceVulkan11Features>();
  result.vulkan12 = feature_chain.get<vk::PhysicalDeviceVulkan12Features>();
  result.vulkan13 = feature_chain.get<vk::PhysicalDeviceVulkan13Features>();
  result.descriptor_buffer = feature_chain.get<vk::PhysicalDeviceDescriptorBufferFeaturesEXT>();
  result.graphics_pipeline_library = feature_chain.get<vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT>();
  result.mesh_shader = feature_chain.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
  result.present_mode_fifo_latest_ready = feature_chain.get<vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesEXT>();

  result.vulkan11.pNext = nullptr;
  result.vulkan12.pNext = nullptr;
  result.vulkan13.pNext = nullptr;
  result.descriptor_buffer.pNext = nullptr;
  result.graphics_pipeline_library.pNext = nullptr;
  result.mesh_shader.pNext = nullptr;
  result.present_mode_fifo_latest_ready.pNext = nullptr;

  return result;
}

[[nodiscard]] physical_device_limit_set query_limits(const vk::raii::PhysicalDevice& device) {
  auto property_chain = device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan12Properties>();

  physical_device_limit_set result{};
  result.core = property_chain.get<vk::PhysicalDeviceProperties2>().properties.limits;
  result.vulkan12 = property_chain.get<vk::PhysicalDeviceVulkan12Properties>();
  result.vulkan12.pNext = nullptr;

  return result;
}

[[nodiscard]] std::vector<queue_family_info> query_queue_families(const vk::raii::PhysicalDevice& device,
                                                                  vk::SurfaceKHR surface) {
  const std::vector<vk::QueueFamilyProperties> properties = device.getQueueFamilyProperties();

  std::vector<queue_family_info> families;
  families.reserve(properties.size());

  for (std::uint32_t index = 0; index < properties.size(); ++index) {
    bool present_supported = false;

    if (surface) {
      present_supported = static_cast<bool>(device.getSurfaceSupportKHR(index, surface));
    }

    families.push_back({
      .index = index,
      .flags = properties.at(index).queueFlags,
      .queue_count = properties.at(index).queueCount,
      .present_supported = present_supported,
    });
  }

  return families;
}

template <typename Predicate>
[[nodiscard]] std::optional<std::uint32_t> find_queue_family_index(const std::vector<queue_family_info>& families,
                                                                   Predicate predicate) {
  for (const queue_family_info& family : families) {
    if (predicate(family)) {
      return family.index;
    }
  }

  return std::nullopt;
}

[[nodiscard]] queue_family_indices select_queue_families(const std::vector<queue_family_info>& families) {
  queue_family_indices result{};

  result.graphics = find_queue_family_index(
      families, [](const queue_family_info& family) { return has_queue_flag(family, vk::QueueFlagBits::eGraphics); });

  result.present =
      find_queue_family_index(families, [](const queue_family_info& family) { return family.present_supported; });

  result.compute = find_queue_family_index(families, [](const queue_family_info& family) {
    return has_queue_flag(family, vk::QueueFlagBits::eCompute) && !has_queue_flag(family, vk::QueueFlagBits::eGraphics);
  });

  if (!result.compute.has_value()) {
    result.compute = find_queue_family_index(
        families, [](const queue_family_info& family) { return has_queue_flag(family, vk::QueueFlagBits::eCompute); });
  }

  result.transfer = find_queue_family_index(families, [](const queue_family_info& family) {
    return has_queue_flag(family, vk::QueueFlagBits::eTransfer) &&
           !has_queue_flag(family, vk::QueueFlagBits::eGraphics) &&
           !has_queue_flag(family, vk::QueueFlagBits::eCompute);
  });

  if (!result.transfer.has_value()) {
    result.transfer = find_queue_family_index(
        families, [](const queue_family_info& family) { return has_queue_flag(family, vk::QueueFlagBits::eTransfer); });
  }

  return result;
}

[[nodiscard]] bool query_swapchain_adequate(const vk::raii::PhysicalDevice& device, vk::SurfaceKHR surface,
                                            const std::vector<std::string>& extensions) {
  if (!surface) {
    return false;
  }

  if (!contains_extension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
    return false;
  }

  const std::vector<vk::SurfaceFormatKHR> formats = device.getSurfaceFormatsKHR(surface);
  const std::vector<vk::PresentModeKHR> present_modes = device.getSurfacePresentModesKHR(surface);

  return !formats.empty() && !present_modes.empty();
}

[[nodiscard]] int device_type_score(vk::PhysicalDeviceType type) noexcept {
  switch (type) {
    case vk::PhysicalDeviceType::eDiscreteGpu:
      return 10000;
    case vk::PhysicalDeviceType::eIntegratedGpu:
      return 6000;
    case vk::PhysicalDeviceType::eVirtualGpu:
      return 3000;
    case vk::PhysicalDeviceType::eCpu:
      return 500;
    case vk::PhysicalDeviceType::eOther:
      return 1000;
    default:
      return 0;
  }
}

[[nodiscard]] int preference_bonus(const physical_device_info& device, gpu_preference preference) noexcept {
  switch (preference) {
    case gpu_preference::automatic:
    case gpu_preference::first_suitable:
      return 0;
    case gpu_preference::discrete:
      return device.properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ? 100000 : 0;
    case gpu_preference::integrated:
      return device.properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ? 100000 : 0;
  }

  return 0;
}

void add_missing(std::vector<std::string>& missing, bool supported, std::string_view requirement) {
  if (!supported) {
    missing.emplace_back(requirement);
  }
}

void populate_enabled_extensions(physical_device_info& info, const device_requirements& requirements) {
  info.optional_features = {};
  info.enabled_extensions.clear();
  info.enabled_extensions.reserve(requirements.required_extensions.size() + 5);

  for (const std::string& required_extension : requirements.required_extensions) {
    append_extension_with_dependencies(info, required_extension);
  }

  if (contains_extension(info.extensions, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) &&
      info.features.descriptor_buffer.descriptorBuffer == vk::True) {
    info.optional_features.descriptor_buffer = true;
    append_extension_with_dependencies(info, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
  }

  if (contains_extension(info.extensions, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME) &&
      contains_extension(info.extensions, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME) &&
      info.features.graphics_pipeline_library.graphicsPipelineLibrary == vk::True) {
    info.optional_features.graphics_pipeline_library = true;
    append_extension_with_dependencies(info, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
  }

  if (contains_extension(info.extensions, VK_EXT_MESH_SHADER_EXTENSION_NAME) &&
      info.features.mesh_shader.meshShader == vk::True) {
    info.optional_features.mesh_shader = true;
    info.optional_features.task_shader = info.features.mesh_shader.taskShader == vk::True;
    append_extension_with_dependencies(info, VK_EXT_MESH_SHADER_EXTENSION_NAME);
  }

  if (contains_extension(info.extensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) {
    info.optional_features.memory_budget = true;
    append_extension_with_dependencies(info, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
  }

  if (contains_extension(info.extensions, VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME) &&
      info.features.present_mode_fifo_latest_ready.presentModeFifoLatestReady == vk::True) {
    info.optional_features.present_mode_fifo_latest_ready = true;
    append_extension_with_dependencies(info, VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
  }

  info.optional_features.scalar_block_layout = info.features.vulkan12.scalarBlockLayout == vk::True;
  info.optional_features.host_query_reset = info.features.vulkan12.hostQueryReset == vk::True;
  info.optional_features.multi_draw_indirect = info.features.core.multiDrawIndirect == vk::True;
}

void collect_queue_requirements(const physical_device_info& info, const device_requirements& requirements,
                                std::vector<std::string>& missing) {
  if (requirements.require_graphics_queue) {
    add_missing(missing, info.selected_queues.graphics.has_value(), "missing graphics queue");
  }

  if (requirements.require_present_queue) {
    add_missing(missing, info.selected_queues.present.has_value(), "missing present queue");
  }
}

void collect_extension_requirements(const physical_device_info& info, const device_requirements& requirements,
                                    std::vector<std::string>& missing) {
  for (const std::string& required_extension : requirements.required_extensions) {
    if (!contains_extension(info.extensions, required_extension)) {
      missing.emplace_back(std::string{"missing device extension: "} + required_extension);
    }

    if (required_extension == VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME &&
        !contains_extension(info.extensions, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME)) {
      missing.emplace_back(std::string{"missing device extension dependency: "} +
                           VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
    }
  }
}

void collect_feature_requirements(const physical_device_info& info, const device_requirements& requirements,
                                  std::vector<std::string>& missing) {
  add_missing(missing, !requirements.require_vulkan_1_3 || info.properties.apiVersion >= VK_API_VERSION_1_3,
              "Vulkan 1.3 is not supported");

  add_missing(missing, !requirements.require_dynamic_rendering || info.features.vulkan13.dynamicRendering == vk::True,
              "dynamic rendering is not supported");

  add_missing(missing, !requirements.require_synchronization2 || info.features.vulkan13.synchronization2 == vk::True,
              "synchronization2 is not supported");

  add_missing(missing, !requirements.require_timeline_semaphore || info.features.vulkan12.timelineSemaphore == vk::True,
              "timeline semaphore is not supported");

  add_missing(missing, !requirements.require_sampler_anisotropy || info.features.core.samplerAnisotropy == vk::True,
              "sampler anisotropy is not supported");

  add_missing(missing,
              !requirements.require_shader_draw_parameters || info.features.vulkan11.shaderDrawParameters == vk::True,
              "shader draw parameters are not supported");

  add_missing(missing,
              !requirements.require_descriptor_indexing || info.features.vulkan12.descriptorIndexing == vk::True,
              "descriptor indexing is not supported");

  add_missing(missing,
              !requirements.require_buffer_device_address || info.features.vulkan12.bufferDeviceAddress == vk::True,
              "buffer device address is not supported");
}

void collect_bindless_descriptor_requirements(const physical_device_info& info, const device_requirements& requirements,
                                              std::vector<std::string>& missing) {
  if (!requirements.require_bindless_descriptors) {
    return;
  }

  add_missing(missing, info.features.vulkan12.runtimeDescriptorArray == vk::True,
              "runtime descriptor arrays are not supported");

  add_missing(missing, info.features.vulkan12.shaderSampledImageArrayNonUniformIndexing == vk::True,
              "sampled image non-uniform indexing is not supported");

  add_missing(missing, info.features.vulkan12.descriptorBindingPartiallyBound == vk::True,
              "partially-bound descriptors are not supported");

  add_missing(missing, info.features.vulkan12.descriptorBindingVariableDescriptorCount == vk::True,
              "variable descriptor counts are not supported");

  add_missing(missing, info.features.vulkan12.descriptorBindingSampledImageUpdateAfterBind == vk::True,
              "sampled image update-after-bind is not supported");

  add_missing(missing, info.features.vulkan12.descriptorBindingStorageBufferUpdateAfterBind == vk::True,
              "storage buffer update-after-bind is not supported");
}

void collect_swapchain_requirements(physical_device_info& info, const vk::raii::PhysicalDevice& device,
                                    vk::SurfaceKHR surface, const device_requirements& requirements,
                                    std::vector<std::string>& missing) {
  if (!requirements.require_swapchain) {
    return;
  }

  info.swapchain_adequate = query_swapchain_adequate(device, surface, info.extensions);
  add_missing(missing, info.swapchain_adequate, "swapchain support is incomplete");
}

void collect_missing_requirements(physical_device_info& info, const vk::raii::PhysicalDevice& device,
                                  vk::SurfaceKHR surface, const device_requirements& requirements,
                                  std::vector<std::string>& missing) {
  collect_queue_requirements(info, requirements, missing);
  collect_extension_requirements(info, requirements, missing);
  collect_swapchain_requirements(info, device, surface, requirements, missing);
  collect_feature_requirements(info, requirements, missing);
  collect_bindless_descriptor_requirements(info, requirements, missing);
}

[[nodiscard]] int feature_score(bool supported, int score) { return supported ? score : 0; }

[[nodiscard]] int calculate_device_score(const physical_device_info& info) {
  const std::uint64_t local_memory_mib = device_local_memory_bytes(info.memory_properties) / (1024ULL * 1024ULL);

  int score = 100;
  score += device_type_score(info.properties.deviceType);
  score += static_cast<int>(std::min<std::uint64_t>(local_memory_mib, 32768ULL) / 16ULL);
  score += static_cast<int>(info.properties.limits.maxImageDimension2D / 1024u);

  score += feature_score(info.features.vulkan13.dynamicRendering == vk::True, 250);
  score += feature_score(info.features.vulkan13.synchronization2 == vk::True, 250);
  score += feature_score(info.features.vulkan12.timelineSemaphore == vk::True, 150);
  score += feature_score(info.features.vulkan12.descriptorIndexing == vk::True, 150);
  score += feature_score(info.features.vulkan12.bufferDeviceAddress == vk::True, 100);

  score += feature_score(info.optional_features.descriptor_buffer, 80);
  score += feature_score(info.optional_features.graphics_pipeline_library, 60);
  score += feature_score(info.optional_features.mesh_shader, 60);

  return score;
}

void evaluate_device(physical_device_info& info, const vk::raii::PhysicalDevice& device, vk::SurfaceKHR surface,
                     const device_requirements& requirements) {
  std::vector<std::string> missing;

  collect_missing_requirements(info, device, surface, requirements, missing);

  info.missing_requirements = std::move(missing);
  info.suitable = info.missing_requirements.empty();

  if (!info.suitable) {
    info.score = -1;
    return;
  }

  populate_enabled_extensions(info, requirements);
  info.score = calculate_device_score(info);
}

}  // namespace

std::vector<std::uint32_t> queue_family_indices::unique_families() const {
  std::vector<std::uint32_t> result;

  const auto append = [&](const std::optional<std::uint32_t>& family) {
    if (family.has_value() && std::ranges::find(result, *family) == result.end()) {
      result.push_back(*family);
    }
  };

  append(graphics);
  append(present);
  append(compute);
  append(transfer);

  return result;
}

std::string api_version_to_string(std::uint32_t version) {
  return std::to_string(VK_VERSION_MAJOR(version)) + "." + std::to_string(VK_VERSION_MINOR(version)) + "." +
         std::to_string(VK_VERSION_PATCH(version));
}

std::string physical_device_name(const vk::PhysicalDeviceProperties& properties) {
  return copy_fixed_c_string(properties.deviceName);
}

std::string physical_device_type_to_string(vk::PhysicalDeviceType type) {
  switch (type) {
    case vk::PhysicalDeviceType::eOther:
      return "other";
    case vk::PhysicalDeviceType::eIntegratedGpu:
      return "integrated_gpu";
    case vk::PhysicalDeviceType::eDiscreteGpu:
      return "discrete_gpu";
    case vk::PhysicalDeviceType::eVirtualGpu:
      return "virtual_gpu";
    case vk::PhysicalDeviceType::eCpu:
      return "cpu";
    default:
      return "unknown";
  }
}

std::string queue_flags_to_string(vk::QueueFlags flags) {
  std::string result;

  const auto append = [&](std::string_view name) {
    if (!result.empty()) {
      result += "|";
    }

    result += name;
  };

  if (static_cast<bool>(flags & vk::QueueFlagBits::eGraphics)) {
    append("graphics");
  }

  if (static_cast<bool>(flags & vk::QueueFlagBits::eCompute)) {
    append("compute");
  }

  if (static_cast<bool>(flags & vk::QueueFlagBits::eTransfer)) {
    append("transfer");
  }

  if (static_cast<bool>(flags & vk::QueueFlagBits::eSparseBinding)) {
    append("sparse");
  }

  if (static_cast<bool>(flags & vk::QueueFlagBits::eProtected)) {
    append("protected");
  }

  if (result.empty()) {
    return "none";
  }

  return result;
}

std::uint64_t device_local_memory_bytes(const vk::PhysicalDeviceMemoryProperties& memory_properties) {
  std::uint64_t result = 0;

  for (std::uint32_t index = 0; index < memory_properties.memoryHeapCount; ++index) {
    const vk::MemoryHeap& heap = memory_properties.memoryHeaps.at(index);

    if (static_cast<bool>(heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal)) {
      result += heap.size;
    }
  }

  return result;
}

std::vector<physical_device_info> enumerate_physical_devices(const vk::raii::PhysicalDevices& devices,
                                                             vk::SurfaceKHR surface,
                                                             const device_requirements& requirements) {
  std::vector<physical_device_info> result;
  result.reserve(devices.size());

  for (const vk::raii::PhysicalDevice& device : devices) {
    physical_device_info info{};
    info.handle = *device;
    info.properties = device.getProperties();
    info.memory_properties = device.getMemoryProperties();
    info.extensions = enumerate_device_extensions(device);
    info.features = query_features(device);
    info.limits = query_limits(device);
    info.queue_families = query_queue_families(device, surface);
    info.selected_queues = select_queue_families(info.queue_families);

    evaluate_device(info, device, surface, requirements);

    result.push_back(std::move(info));
  }

  return result;
}

std::optional<std::size_t> choose_physical_device(const std::vector<physical_device_info>& devices,
                                                  gpu_preference preference,
                                                  std::optional<std::size_t> preferred_index) {
  if (preferred_index.has_value() && *preferred_index < devices.size() && devices.at(*preferred_index).suitable) {
    return preferred_index;
  }

  if (preference == gpu_preference::first_suitable) {
    for (std::size_t index = 0; index < devices.size(); ++index) {
      if (devices.at(index).suitable) {
        return index;
      }
    }

    return std::nullopt;
  }

  std::optional<std::size_t> best_index;
  int best_score = std::numeric_limits<int>::min();

  for (std::size_t index = 0; index < devices.size(); ++index) {
    const physical_device_info& device = devices.at(index);

    if (!device.suitable) {
      continue;
    }

    const int score = device.score + preference_bonus(device, preference);

    if (!best_index.has_value() || score > best_score) {
      best_index = index;
      best_score = score;
    }
  }

  return best_index;
}

}  // namespace rl::vulkan
