#include "vk/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "base/log.hpp"
#include "platform/sdl_window.hpp"
#include "vk/device.hpp"
#include "vk/physical_device.hpp"

namespace rl::vulkan {
namespace {

constexpr std::array<std::string_view, 1> validation_layers = {
  "VK_LAYER_KHRONOS_validation",
};

template <typename FixedString>
[[nodiscard]] std::string copy_fixed_c_string(const FixedString& value) {
  if constexpr (requires { value.data(); }) {
    return std::string{value.data()};
  } else {
    return std::string{value};
  }
}

[[nodiscard]] std::string optional_index_to_string(const std::optional<std::uint32_t>& value) {
  if (!value.has_value()) {
    return "none";
  }

  return std::to_string(*value);
}

[[nodiscard]] bool contains_name(const std::vector<std::string>& values, std::string_view value) {
  return std::ranges::any_of(values, [&](const std::string& current) { return current == value; });
}

void append_unique(std::vector<std::string>& values, std::string_view value) {
  if (!contains_name(values, value)) {
    values.emplace_back(value);
  }
}

[[nodiscard]] std::vector<const char*> to_vulkan_name_pointers(const std::vector<std::string>& names) {
  std::vector<const char*> result;
  result.reserve(names.size());

  for (const std::string& name : names) {
    result.push_back(name.c_str());
  }

  return result;
}

[[nodiscard]] std::string_view callback_text(const char* value, std::string_view fallback) noexcept {
  return value == nullptr ? fallback : std::string_view{value};
}

[[nodiscard]] std::string debug_message_type_to_string(vk::DebugUtilsMessageTypeFlagsEXT message_type) {
  std::string result;

  const auto append = [&](std::string_view name) {
    if (!result.empty()) {
      result += "|";
    }

    result += name;
  };

  if (static_cast<bool>(message_type & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral)) {
    append("general");
  }

  if (static_cast<bool>(message_type & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation)) {
    append("validation");
  }

  if (static_cast<bool>(message_type & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)) {
    append("performance");
  }

  if (result.empty()) {
    return "unknown";
  }

  return result;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL vulkan_debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                       vk::DebugUtilsMessageTypeFlagsEXT message_type,
                                                       const vk::DebugUtilsMessengerCallbackDataEXT* callback_data,
                                                       [[maybe_unused]] void* data) {
  const std::string type = debug_message_type_to_string(message_type);

  const std::string_view message_id =
      callback_data == nullptr ? "unknown" : callback_text(callback_data->pMessageIdName, "unknown");

  const std::string_view message =
      callback_data == nullptr ? "<no message>" : callback_text(callback_data->pMessage, "<no message>");

  if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
    RL_VK_ERROR("validation [{}:{}]: {}", type, message_id, message);
  } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
    RL_VK_WARN("validation [{}:{}]: {}", type, message_id, message);
  } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
    RL_VK_INFO("validation [{}:{}]: {}", type, message_id, message);
  } else {
    RL_VK_TRACE("validation [{}:{}]: {}", type, message_id, message);
  }

  return vk::False;
}

[[nodiscard]] vk::DebugUtilsMessengerCreateInfoEXT make_debug_messenger_create_info() {
  vk::DebugUtilsMessengerCreateInfoEXT create_info{};

  create_info.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;

  create_info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;

  create_info.pfnUserCallback = vulkan_debug_callback;

  return create_info;
}

}  // namespace

vulkan_context::vulkan_context(const platform::sdl_window& window, vulkan_context_config config)
    : config_{std::move(config)} {
  RL_VK_INFO("initializing Vulkan context with vk::raii");

  create_instance(window);
  create_surface(window);
  enumerate_and_select_physical_device();
  create_logical_device();

  RL_VK_INFO("Vulkan context initialized");
}

vulkan_context::~vulkan_context() noexcept { RL_VK_DEBUG("destroying Vulkan context"); }

const vk::raii::Context& vulkan_context::context() const noexcept { return context_; }

const vk::raii::Instance& vulkan_context::instance() const noexcept { return instance_; }

const vk::raii::SurfaceKHR& vulkan_context::surface() const noexcept { return surface_; }

const device& vulkan_context::device() const noexcept {
  if (!device_.has_value()) {
    std::terminate();
  }

  return *device_;
}

vk::Instance vulkan_context::raw_instance() const noexcept { return *instance_; }

vk::SurfaceKHR vulkan_context::raw_surface() const noexcept { return *surface_; }

vk::PhysicalDevice vulkan_context::physical_device() const {
  if (physical_devices_.empty()) {
    return {};
  }

  return physical_devices_.at(selected_physical_device_index_).handle;
}

vk::PhysicalDevice vulkan_context::raw_physical_device() const { return physical_device(); }

vk::Device vulkan_context::raw_device() const noexcept { return device().raw_handle(); }

VkInstance vulkan_context::c_instance() const noexcept { return static_cast<VkInstance>(*instance_); }

VkSurfaceKHR vulkan_context::c_surface() const noexcept { return static_cast<VkSurfaceKHR>(*surface_); }

VkPhysicalDevice vulkan_context::c_physical_device() const { return static_cast<VkPhysicalDevice>(physical_device()); }

VkDevice vulkan_context::c_device() const noexcept { return device().c_handle(); }

const std::vector<physical_device_info>& vulkan_context::physical_devices() const noexcept { return physical_devices_; }

const physical_device_info& vulkan_context::selected_physical_device() const {
  return physical_devices_.at(selected_physical_device_index_);
}

const vk::raii::PhysicalDevice& vulkan_context::selected_physical_device_handle() const {
  if (!physical_device_handles_.has_value()) {
    throw std::runtime_error{"physical devices have not been enumerated"};
  }

  return physical_device_handles_->at(selected_physical_device_index_);
}

std::size_t vulkan_context::selected_physical_device_index() const noexcept { return selected_physical_device_index_; }

bool vulkan_context::validation_layers_available() const {
  const std::vector<vk::LayerProperties> available_layers = context_.enumerateInstanceLayerProperties();

  for (std::string_view required_layer : validation_layers) {
    const bool found = std::ranges::any_of(available_layers, [&](const vk::LayerProperties& layer) {
      return copy_fixed_c_string(layer.layerName) == required_layer;
    });

    if (!found) {
      return false;
    }
  }

  return true;
}

bool vulkan_context::instance_extension_available(std::string_view extension_name) const {
  const std::vector<vk::ExtensionProperties> extensions = context_.enumerateInstanceExtensionProperties();

  return std::ranges::any_of(extensions, [&](const vk::ExtensionProperties& extension) {
    return copy_fixed_c_string(extension.extensionName) == extension_name;
  });
}

void vulkan_context::create_instance([[maybe_unused]] const platform::sdl_window& window) {
  RL_VK_INFO("creating Vulkan instance");

  const std::uint32_t loader_api_version = context_.enumerateInstanceVersion();

  RL_VK_INFO("Vulkan loader API version: {}", api_version_to_string(loader_api_version));

  if (config_.requirements.require_vulkan_1_3 && loader_api_version < VK_API_VERSION_1_3) {
    throw std::runtime_error{"Vulkan 1.3 is required but the Vulkan loader only supports " +
                             api_version_to_string(loader_api_version)};
  }

  validation_enabled_ = config_.enable_validation && validation_layers_available();

  if (config_.enable_validation && !validation_enabled_) {
    RL_VK_WARN("validation requested but VK_LAYER_KHRONOS_validation is not available");
  }

  std::vector<std::string> extensions = platform::sdl_window::required_vulkan_extensions();

  if (validation_enabled_) {
    debug_utils_enabled_ = instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    if (debug_utils_enabled_) {
      append_unique(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else {
      RL_VK_WARN("VK_EXT_debug_utils is not available; validation messages will not be routed");
    }
  }

  std::vector<std::string> enabled_layers;

  if (validation_enabled_) {
    enabled_layers.reserve(validation_layers.size());

    for (const std::string_view layer : validation_layers) {
      enabled_layers.emplace_back(layer);
    }
  }

  const std::uint32_t requested_api_version =
      loader_api_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : loader_api_version;

  vk::ApplicationInfo app_info{};
  app_info.pApplicationName = "Vulkan RenderLab";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.pEngineName = "RenderLab";
  app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.apiVersion = requested_api_version;

  RL_VK_INFO("requested Vulkan API version: {}", api_version_to_string(app_info.apiVersion));

  for (std::size_t index = 0; index < extensions.size(); ++index) {
    RL_VK_TRACE("instance extension[{}]: {}", index, extensions.at(index));
  }

  for (std::size_t index = 0; index < enabled_layers.size(); ++index) {
    RL_VK_TRACE("instance layer[{}]: {}", index, enabled_layers.at(index));
  }

  const std::vector<const char*> extension_names = to_vulkan_name_pointers(extensions);
  const std::vector<const char*> layer_names = to_vulkan_name_pointers(enabled_layers);

  vk::DebugUtilsMessengerCreateInfoEXT debug_create_info{};

  vk::InstanceCreateInfo create_info{};
  create_info.pApplicationInfo = &app_info;
  create_info.setPEnabledExtensionNames(extension_names);
  create_info.setPEnabledLayerNames(layer_names);

  if (debug_utils_enabled_) {
    debug_create_info = make_debug_messenger_create_info();
    create_info.pNext = &debug_create_info;
  }

  instance_ = vk::raii::Instance{context_, create_info};

  RL_VK_INFO("Vulkan instance created: validation={}, debug_utils={}", validation_enabled_, debug_utils_enabled_);

  create_debug_messenger();
}

void vulkan_context::create_debug_messenger() {
  if (!debug_utils_enabled_) {
    return;
  }

  const vk::DebugUtilsMessengerCreateInfoEXT create_info = make_debug_messenger_create_info();

  debug_messenger_ = vk::raii::DebugUtilsMessengerEXT{instance_, create_info};

  RL_VK_INFO("Vulkan debug messenger created");
}

void vulkan_context::create_surface(const platform::sdl_window& window) {
  VkSurfaceKHR raw_surface = window.create_vulkan_surface(c_instance());

  surface_ = vk::raii::SurfaceKHR{instance_, raw_surface};

  RL_VK_INFO("Vulkan presentation surface created");
}

void vulkan_context::enumerate_and_select_physical_device() {
  RL_GPU_INFO("enumerating Vulkan physical devices");

  physical_device_handles_.emplace(instance_);

  physical_devices_ = enumerate_physical_devices(*physical_device_handles_, raw_surface(), config_.requirements);

  if (physical_devices_.empty()) {
    throw std::runtime_error{"no Vulkan-capable physical device found"};
  }

  RL_GPU_INFO("found {} Vulkan physical device(s)", physical_devices_.size());

  for (std::size_t index = 0; index < physical_devices_.size(); ++index) {
    const physical_device_info& device = physical_devices_.at(index);

    const std::uint64_t local_memory_mib = device_local_memory_bytes(device.memory_properties) / (1024ULL * 1024ULL);

    RL_GPU_INFO("GPU[{}]: '{}' type={} api={} vendor=0x{:04x} device=0x{:04x} local_memory={} MiB score={} suitable={}",
                index, physical_device_name(device.properties),
                physical_device_type_to_string(device.properties.deviceType),
                api_version_to_string(device.properties.apiVersion), device.properties.vendorID,
                device.properties.deviceID, local_memory_mib, device.score, device.suitable);

    RL_GPU_DEBUG("GPU[{}] selected queues: graphics={}, present={}, compute={}, transfer={}", index,
                 optional_index_to_string(device.selected_queues.graphics),
                 optional_index_to_string(device.selected_queues.present),
                 optional_index_to_string(device.selected_queues.compute),
                 optional_index_to_string(device.selected_queues.transfer));

    RL_GPU_DEBUG(
        "GPU[{}] features: dynamic_rendering={}, synchronization2={}, timeline_semaphore={}, descriptor_indexing={}, "
        "runtime_descriptor_array={}, buffer_device_address={}, sampler_anisotropy={}",
        index, device.features.vulkan13.dynamicRendering == vk::True,
        device.features.vulkan13.synchronization2 == vk::True, device.features.vulkan12.timelineSemaphore == vk::True,
        device.features.vulkan12.descriptorIndexing == vk::True,
        device.features.vulkan12.runtimeDescriptorArray == vk::True,
        device.features.vulkan12.bufferDeviceAddress == vk::True, device.features.core.samplerAnisotropy == vk::True);

    RL_GPU_DEBUG(
        "GPU[{}] optional features: descriptor_buffer={}, graphics_pipeline_library={}, mesh_shader={}, "
        "task_shader={}, "
        "memory_budget={}, scalar_block_layout={}, host_query_reset={}, multi_draw_indirect={}",
        index, device.optional_features.descriptor_buffer, device.optional_features.graphics_pipeline_library,
        device.optional_features.mesh_shader, device.optional_features.task_shader,
        device.optional_features.memory_budget, device.optional_features.scalar_block_layout,
        device.optional_features.host_query_reset, device.optional_features.multi_draw_indirect);

    for (const queue_family_info& family : device.queue_families) {
      RL_GPU_TRACE("GPU[{}] queue_family[{}]: flags={} queues={} present={}", index, family.index,
                   queue_flags_to_string(family.flags), family.queue_count, family.present_supported);
    }

    for (const std::string& missing : device.missing_requirements) {
      RL_GPU_WARN("GPU[{}] unsuitable: {}", index, missing);
    }
  }

  if (config_.preferred_device_index.has_value()) {
    const std::size_t preferred = *config_.preferred_device_index;

    if (preferred >= physical_devices_.size()) {
      RL_GPU_WARN("preferred GPU index {} is out of range; falling back to configured selection", preferred);
    } else if (!physical_devices_.at(preferred).suitable) {
      RL_GPU_WARN("preferred GPU index {} is unsuitable; falling back to configured selection", preferred);
    }
  }

  const std::optional<std::size_t> selected_index =
      choose_physical_device(physical_devices_, config_.preference, config_.preferred_device_index);

  if (!selected_index.has_value()) {
    throw std::runtime_error{"no suitable Vulkan physical device found"};
  }

  selected_physical_device_index_ = *selected_index;

  const physical_device_info& selected = physical_devices_.at(selected_physical_device_index_);

  RL_GPU_INFO("selected GPU[{}]: '{}'", selected_physical_device_index_, physical_device_name(selected.properties));
}

void vulkan_context::create_logical_device() {
  device_.emplace(selected_physical_device_handle(), selected_physical_device(), config_.requirements);
}

}  // namespace rl::vulkan
