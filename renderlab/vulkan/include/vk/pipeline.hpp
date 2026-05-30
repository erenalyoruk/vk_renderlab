#pragma once

#include <cstddef>
#include <functional>
#include <span>

#include <vulkan/vulkan_raii.hpp>

namespace rl::vulkan {

class shader_module final {
 public:
  shader_module() = default;
  shader_module(const vk::raii::Device& device, std::span<const std::byte> spirv);
  ~shader_module() noexcept = default;

  shader_module(const shader_module& other) = delete;
  shader_module& operator=(const shader_module& other) = delete;

  shader_module(shader_module&& other) noexcept = default;
  shader_module& operator=(shader_module&& other) noexcept = default;

  [[nodiscard]] const vk::raii::ShaderModule& handle() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept;

 private:
  vk::raii::ShaderModule handle_{nullptr};
};

struct graphics_pipeline_description {
  std::reference_wrapper<const shader_module> vertex_shader;
  std::reference_wrapper<const shader_module> fragment_shader;
  vk::Format color_format = vk::Format::eUndefined;
  vk::Format depth_format = vk::Format::eUndefined;
};

class graphics_pipeline final {
 public:
  graphics_pipeline() = default;
  graphics_pipeline(const vk::raii::Device& device, const graphics_pipeline_description& description);
  ~graphics_pipeline() noexcept = default;

  graphics_pipeline(const graphics_pipeline& other) = delete;
  graphics_pipeline& operator=(const graphics_pipeline& other) = delete;

  graphics_pipeline(graphics_pipeline&& other) noexcept = default;
  graphics_pipeline& operator=(graphics_pipeline&& other) noexcept = default;

  [[nodiscard]] const vk::raii::Pipeline& handle() const noexcept;
  [[nodiscard]] const vk::raii::PipelineLayout& layout() const noexcept;
  [[nodiscard]] vk::Format color_format() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept;

 private:
  vk::raii::PipelineLayout layout_{nullptr};
  vk::raii::Pipeline handle_{nullptr};
  vk::Format color_format_ = vk::Format::eUndefined;
};

}  // namespace rl::vulkan
