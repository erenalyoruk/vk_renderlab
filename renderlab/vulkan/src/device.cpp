#include "vk/device.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <string_view>
#include <vector>

#include "base/log.hpp"

namespace rl::vulkan {

namespace {

template <typename VkStruct>
void append_pnext(void** tail, VkStruct& next) noexcept {
  *tail = &next;
  tail = &next.pNext;
}

[[nodiscard]] bool extension_enabled(const physical_device_info& physical_device, const char* extension_name) {
  return std::ranges::any_of(physical_device.enabled_extensions, [&](const char* enabled_extension) {
    return std::string_view{enabled_extension} == extension_name;
  });
}

}  // namespace

device::device(const vk::raii::PhysicalDevice& physical_device_handle, const physical_device_info& physical_device,
               const device_requirements& requirements)
    : queues_{physical_device.selected_queues}, optional_features_{physical_device.optional_features} {
  RL_VK_INFO("creating Vulkan logical device for '{}'", physical_device_name(physical_device.properties));

  constexpr float queue_priority = 1.0f;
  std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
  queue_create_infos.reserve(queues_.unique_families().size());

  for (const std::uint32_t queue_family : queues_.unique_families()) {
    vk::DeviceQueueCreateInfo queue_create_info{};
    queue_create_info.queueFamilyIndex = queue_family;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    queue_create_infos.push_back(queue_create_info);
  }

  vk::PhysicalDeviceVulkan11Features vulkan11_features{};
  vulkan11_features.shaderDrawParameters =
      static_cast<vk::Bool32>(static_cast<uint32_t>(requirements.require_shader_draw_parameters) == vk::True ||
                              physical_device.features.vulkan11.shaderDrawParameters == vk::True);

  vk::PhysicalDeviceVulkan12Features vulkan12_features{};
  vulkan12_features.timelineSemaphore = static_cast<vk::Bool32>(requirements.require_timeline_semaphore);
  vulkan12_features.bufferDeviceAddress = static_cast<vk::Bool32>(requirements.require_buffer_device_address);
  vulkan12_features.descriptorIndexing = static_cast<vk::Bool32>(requirements.require_descriptor_indexing);
  vulkan12_features.runtimeDescriptorArray = static_cast<vk::Bool32>(requirements.require_bindless_descriptors);
  vulkan12_features.shaderSampledImageArrayNonUniformIndexing =
      static_cast<vk::Bool32>(requirements.require_bindless_descriptors);
  vulkan12_features.descriptorBindingPartiallyBound =
      static_cast<vk::Bool32>(requirements.require_bindless_descriptors);
  vulkan12_features.descriptorBindingVariableDescriptorCount =
      static_cast<vk::Bool32>(requirements.require_bindless_descriptors);
  vulkan12_features.descriptorBindingSampledImageUpdateAfterBind =
      static_cast<vk::Bool32>(requirements.require_bindless_descriptors);
  vulkan12_features.descriptorBindingStorageBufferUpdateAfterBind =
      static_cast<vk::Bool32>(requirements.require_bindless_descriptors);
  vulkan12_features.scalarBlockLayout = static_cast<vk::Bool32>(physical_device.optional_features.scalar_block_layout);
  vulkan12_features.hostQueryReset = static_cast<vk::Bool32>(physical_device.optional_features.host_query_reset);

  vk::PhysicalDeviceVulkan13Features vulkan13_features{};
  vulkan13_features.dynamicRendering = static_cast<vk::Bool32>(requirements.require_dynamic_rendering);
  vulkan13_features.synchronization2 = static_cast<vk::Bool32>(requirements.require_synchronization2);
  vulkan13_features.maintenance4 = static_cast<vk::Bool32>(physical_device.features.vulkan13.maintenance4 == vk::True);

  vk::PhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features{};
  descriptor_buffer_features.descriptorBuffer =
      static_cast<vk::Bool32>(physical_device.optional_features.descriptor_buffer);

  vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT graphics_pipeline_library_features{};
  graphics_pipeline_library_features.graphicsPipelineLibrary =
      static_cast<vk::Bool32>(physical_device.optional_features.graphics_pipeline_library);

  vk::PhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features{};
  mesh_shader_features.meshShader = static_cast<vk::Bool32>(physical_device.optional_features.mesh_shader);
  mesh_shader_features.taskShader = static_cast<vk::Bool32>(physical_device.optional_features.task_shader);

  vk::PhysicalDeviceFeatures2 enabled_features{};
  enabled_features.features.samplerAnisotropy = static_cast<vk::Bool32>(requirements.require_sampler_anisotropy);
  enabled_features.features.drawIndirectFirstInstance = physical_device.features.core.drawIndirectFirstInstance;
  enabled_features.features.multiDrawIndirect =
      static_cast<vk::Bool32>(physical_device.optional_features.multi_draw_indirect);

  void** tail = &enabled_features.pNext;
  append_pnext(tail, vulkan11_features);
  tail = &vulkan11_features.pNext;
  append_pnext(tail, vulkan12_features);
  tail = &vulkan12_features.pNext;
  append_pnext(tail, vulkan13_features);
  tail = &vulkan13_features.pNext;

  if (extension_enabled(physical_device, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)) {
    append_pnext(tail, descriptor_buffer_features);
    tail = &descriptor_buffer_features.pNext;
  }

  if (extension_enabled(physical_device, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME)) {
    append_pnext(tail, graphics_pipeline_library_features);
    tail = &graphics_pipeline_library_features.pNext;
  }

  if (extension_enabled(physical_device, VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
    append_pnext(tail, mesh_shader_features);
  }

  for (std::size_t index = 0; index < physical_device.enabled_extensions.size(); ++index) {
    RL_VK_TRACE("device extension[{}]: {}", index, physical_device.enabled_extensions[index]);
  }

  vk::DeviceCreateInfo create_info{};
  create_info.pNext = &enabled_features;
  create_info.queueCreateInfoCount = static_cast<std::uint32_t>(queue_create_infos.size());
  create_info.pQueueCreateInfos = queue_create_infos.data();
  create_info.enabledExtensionCount = static_cast<std::uint32_t>(physical_device.enabled_extensions.size());
  create_info.ppEnabledExtensionNames = physical_device.enabled_extensions.data();

  handle_ = vk::raii::Device{physical_device_handle, create_info};
  graphics_queue_ = vk::raii::Queue{handle_, *queues_.graphics, 0};
  present_queue_ = vk::raii::Queue{handle_, *queues_.present, 0};

  if (queues_.compute.has_value()) {
    compute_queue_.emplace(handle_, *queues_.compute, 0);
  }

  if (queues_.transfer.has_value()) {
    transfer_queue_.emplace(handle_, *queues_.transfer, 0);
  }

  RL_VK_INFO("Vulkan logical device created");
}

device::~device() noexcept {
  try {
    if (raw_handle()) {
      handle_.waitIdle();
    }
  } catch (const std::exception& error) {
    RL_VK_WARN("device wait idle during destruction failed: {}", error.what());
  }
}

const vk::raii::Device& device::handle() const noexcept { return handle_; }

vk::Device device::raw_handle() const noexcept { return *handle_; }

VkDevice device::c_handle() const noexcept { return static_cast<VkDevice>(*handle_); }

const vk::raii::Queue& device::graphics_queue() const noexcept { return graphics_queue_; }

const vk::raii::Queue& device::present_queue() const noexcept { return present_queue_; }

const std::optional<vk::raii::Queue>& device::compute_queue() const noexcept { return compute_queue_; }

const std::optional<vk::raii::Queue>& device::transfer_queue() const noexcept { return transfer_queue_; }

VkQueue device::c_graphics_queue() const noexcept { return static_cast<VkQueue>(*graphics_queue_); }

VkQueue device::c_present_queue() const noexcept { return static_cast<VkQueue>(*present_queue_); }

VkQueue device::c_compute_queue() const noexcept {
  if (!compute_queue_.has_value()) {
    return VK_NULL_HANDLE;
  }

  return static_cast<VkQueue>(**compute_queue_);
}

VkQueue device::c_transfer_queue() const noexcept {
  if (!transfer_queue_.has_value()) {
    return VK_NULL_HANDLE;
  }

  return static_cast<VkQueue>(**transfer_queue_);
}

const queue_family_indices& device::queues() const noexcept { return queues_; }

const optional_device_features& device::optional_features() const noexcept { return optional_features_; }

}  // namespace rl::vulkan
