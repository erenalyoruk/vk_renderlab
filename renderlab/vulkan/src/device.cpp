#include "vk/device.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "base/log.hpp"
#include "vk/physical_device.hpp"

namespace rl::vulkan {

namespace {

using device_feature_chain =
    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceDescriptorBufferFeaturesEXT,
                       vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT>;

[[nodiscard]] bool extension_enabled(const physical_device_info& physical_device, std::string_view extension_name) {
  return std::ranges::any_of(physical_device.enabled_extensions,
                             [&](const std::string& enabled_extension) { return enabled_extension == extension_name; });
}

[[nodiscard]] std::vector<const char*> to_vulkan_name_pointers(const std::vector<std::string>& names) {
  std::vector<const char*> result;
  result.reserve(names.size());

  for (const std::string& name : names) {
    result.push_back(name.c_str());
  }

  return result;
}

}  // namespace

device::device(const vk::raii::PhysicalDevice& physical_device_handle, const physical_device_info& physical_device,
               const device_requirements& requirements)
    : queues_{physical_device.selected_queues}, optional_features_{physical_device.optional_features} {
  RL_VK_INFO("creating Vulkan logical device for '{}'", physical_device_name(physical_device.properties));

  constexpr std::array queue_priorities = {1.0f};
  std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
  queue_create_infos.reserve(queues_.unique_families().size());

  for (const std::uint32_t queue_family : queues_.unique_families()) {
    vk::DeviceQueueCreateInfo queue_create_info{};
    queue_create_info.queueFamilyIndex = queue_family;
    queue_create_info.setQueuePriorities(queue_priorities);
    queue_create_infos.push_back(queue_create_info);
  }

  device_feature_chain feature_chain{};

  auto& enabled_features = feature_chain.get<vk::PhysicalDeviceFeatures2>();
  auto& vulkan11_features = feature_chain.get<vk::PhysicalDeviceVulkan11Features>();
  auto& vulkan12_features = feature_chain.get<vk::PhysicalDeviceVulkan12Features>();
  auto& vulkan13_features = feature_chain.get<vk::PhysicalDeviceVulkan13Features>();
  auto& descriptor_buffer_features = feature_chain.get<vk::PhysicalDeviceDescriptorBufferFeaturesEXT>();
  auto& graphics_pipeline_library_features = feature_chain.get<vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT>();
  auto& mesh_shader_features = feature_chain.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();

  vulkan11_features.shaderDrawParameters =
      static_cast<vk::Bool32>(requirements.require_shader_draw_parameters ||
                              physical_device.features.vulkan11.shaderDrawParameters == vk::True);

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

  vulkan13_features.dynamicRendering = static_cast<vk::Bool32>(requirements.require_dynamic_rendering);
  vulkan13_features.synchronization2 = static_cast<vk::Bool32>(requirements.require_synchronization2);
  vulkan13_features.maintenance4 = static_cast<vk::Bool32>(physical_device.features.vulkan13.maintenance4 == vk::True);

  descriptor_buffer_features.descriptorBuffer =
      static_cast<vk::Bool32>(physical_device.optional_features.descriptor_buffer);

  graphics_pipeline_library_features.graphicsPipelineLibrary =
      static_cast<vk::Bool32>(physical_device.optional_features.graphics_pipeline_library);

  mesh_shader_features.meshShader = static_cast<vk::Bool32>(physical_device.optional_features.mesh_shader);
  mesh_shader_features.taskShader = static_cast<vk::Bool32>(physical_device.optional_features.task_shader);

  enabled_features.features.samplerAnisotropy = static_cast<vk::Bool32>(requirements.require_sampler_anisotropy);
  enabled_features.features.drawIndirectFirstInstance = physical_device.features.core.drawIndirectFirstInstance;
  enabled_features.features.multiDrawIndirect =
      static_cast<vk::Bool32>(physical_device.optional_features.multi_draw_indirect);

  if (!extension_enabled(physical_device, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)) {
    feature_chain.unlink<vk::PhysicalDeviceDescriptorBufferFeaturesEXT>();
  }

  if (!extension_enabled(physical_device, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME)) {
    feature_chain.unlink<vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT>();
  }

  if (!extension_enabled(physical_device, VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
    feature_chain.unlink<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
  }

  for (std::size_t index = 0; index < physical_device.enabled_extensions.size(); ++index) {
    RL_VK_TRACE("device extension[{}]: {}", index, physical_device.enabled_extensions.at(index));
  }

  const std::vector<const char*> enabled_extension_names = to_vulkan_name_pointers(physical_device.enabled_extensions);

  vk::DeviceCreateInfo create_info{};
  create_info.pNext = &enabled_features;
  create_info.setQueueCreateInfos(queue_create_infos);
  create_info.setPEnabledExtensionNames(enabled_extension_names);

  handle_ = vk::raii::Device{physical_device_handle, create_info};

  if (!queues_.graphics.has_value() || !queues_.present.has_value()) {
    throw std::runtime_error{"logical device requires graphics and present queue families"};
  }

  graphics_queue_ = vk::raii::Queue{handle_, queues_.graphics.value(), 0};
  present_queue_ = vk::raii::Queue{handle_, queues_.present.value(), 0};

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
