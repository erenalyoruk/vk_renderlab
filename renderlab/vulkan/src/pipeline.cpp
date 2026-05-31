#include "vk/pipeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

namespace rl::vulkan {
namespace {

constexpr std::string_view vertex_entry_point = "vertexMain";
constexpr std::string_view fragment_entry_point = "fragmentMain";

[[nodiscard]] std::vector<std::uint32_t> spirv_words(std::span<const std::byte> spirv) {
  if (spirv.empty()) {
    throw std::runtime_error{"shader module requires non-empty SPIR-V bytecode"};
  }

  if ((spirv.size_bytes() % sizeof(std::uint32_t)) != 0u) {
    throw std::runtime_error{"shader module SPIR-V bytecode size must be aligned to 32-bit words"};
  }

  std::vector<std::uint32_t> words(spirv.size_bytes() / sizeof(std::uint32_t));
  std::memcpy(words.data(), spirv.data(), spirv.size_bytes());
  return words;
}

[[nodiscard]] vk::PipelineShaderStageCreateInfo make_shader_stage(vk::ShaderStageFlagBits stage,
                                                                  const shader_module& module,
                                                                  std::string_view entry_point) {
  if (!module) {
    throw std::runtime_error{"graphics pipeline requires valid shader modules"};
  }

  vk::PipelineShaderStageCreateInfo create_info{};
  create_info.stage = stage;
  create_info.module = *module.handle();
  create_info.pName = entry_point.data();
  return create_info;
}

[[nodiscard]] vk::PipelineVertexInputStateCreateInfo make_vertex_input_state(
    const graphics_pipeline_description& description) noexcept {
  vk::PipelineVertexInputStateCreateInfo create_info{};
  create_info.setVertexBindingDescriptions(description.vertex_bindings);
  create_info.setVertexAttributeDescriptions(description.vertex_attributes);
  return create_info;
}

[[nodiscard]] vk::PipelineInputAssemblyStateCreateInfo make_input_assembly_state() noexcept {
  vk::PipelineInputAssemblyStateCreateInfo create_info{};
  create_info.topology = vk::PrimitiveTopology::eTriangleList;
  create_info.primitiveRestartEnable = vk::False;
  return create_info;
}

[[nodiscard]] vk::PipelineViewportStateCreateInfo make_viewport_state() noexcept {
  vk::PipelineViewportStateCreateInfo create_info{};
  create_info.viewportCount = 1;
  create_info.scissorCount = 1;
  return create_info;
}

[[nodiscard]] vk::PipelineRasterizationStateCreateInfo make_rasterization_state() noexcept {
  vk::PipelineRasterizationStateCreateInfo create_info{};
  create_info.depthClampEnable = vk::False;
  create_info.rasterizerDiscardEnable = vk::False;
  create_info.polygonMode = vk::PolygonMode::eFill;
  create_info.cullMode = vk::CullModeFlagBits::eNone;
  create_info.frontFace = vk::FrontFace::eCounterClockwise;
  create_info.depthBiasEnable = vk::False;
  create_info.lineWidth = 1.0f;
  return create_info;
}

[[nodiscard]] vk::PipelineMultisampleStateCreateInfo make_multisample_state() noexcept {
  vk::PipelineMultisampleStateCreateInfo create_info{};
  create_info.rasterizationSamples = vk::SampleCountFlagBits::e1;
  create_info.sampleShadingEnable = vk::False;
  return create_info;
}

[[nodiscard]] vk::PipelineDepthStencilStateCreateInfo make_depth_stencil_state(vk::Format depth_format) noexcept {
  vk::PipelineDepthStencilStateCreateInfo create_info{};
  create_info.depthTestEnable = depth_format == vk::Format::eUndefined ? vk::False : vk::True;
  create_info.depthWriteEnable = depth_format == vk::Format::eUndefined ? vk::False : vk::True;
  create_info.depthCompareOp = vk::CompareOp::eLessOrEqual;
  create_info.depthBoundsTestEnable = vk::False;
  create_info.stencilTestEnable = vk::False;
  return create_info;
}

[[nodiscard]] vk::PipelineColorBlendAttachmentState make_color_blend_attachment() noexcept {
  vk::PipelineColorBlendAttachmentState attachment{};
  attachment.blendEnable = vk::False;
  attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  return attachment;
}

}  // namespace

shader_module::shader_module(const vk::raii::Device& device, std::span<const std::byte> spirv) {
  const std::vector<std::uint32_t> words = spirv_words(spirv);

  vk::ShaderModuleCreateInfo create_info{};
  create_info.setCode(words);

  handle_ = vk::raii::ShaderModule{device, create_info};
}

const vk::raii::ShaderModule& shader_module::handle() const noexcept { return handle_; }

shader_module::operator bool() const noexcept { return static_cast<bool>(*handle_); }

graphics_pipeline::graphics_pipeline(const vk::raii::Device& device, const graphics_pipeline_description& description)
    : color_format_{description.color_format} {
  if (color_format_ == vk::Format::eUndefined) {
    throw std::runtime_error{"graphics pipeline requires a concrete color format"};
  }

  vk::PipelineLayoutCreateInfo layout_create_info{};
  layout_create_info.setSetLayouts(description.descriptor_set_layouts);
  layout_ = vk::raii::PipelineLayout{device, layout_create_info};

  const std::array shader_stages = {
    make_shader_stage(vk::ShaderStageFlagBits::eVertex, description.vertex_shader.get(), vertex_entry_point),
    make_shader_stage(vk::ShaderStageFlagBits::eFragment, description.fragment_shader.get(), fragment_entry_point),
  };

  vk::PipelineRenderingCreateInfo rendering_info{};
  rendering_info.setColorAttachmentFormats(color_format_);
  rendering_info.depthAttachmentFormat = description.depth_format;

  const vk::PipelineVertexInputStateCreateInfo vertex_input_state = make_vertex_input_state(description);
  const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state = make_input_assembly_state();
  const vk::PipelineViewportStateCreateInfo viewport_state = make_viewport_state();
  const vk::PipelineRasterizationStateCreateInfo rasterization_state = make_rasterization_state();
  const vk::PipelineMultisampleStateCreateInfo multisample_state = make_multisample_state();
  const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
      make_depth_stencil_state(description.depth_format);
  const vk::PipelineColorBlendAttachmentState color_blend_attachment = make_color_blend_attachment();

  vk::PipelineColorBlendStateCreateInfo color_blend_state{};
  color_blend_state.setAttachments(color_blend_attachment);

  const std::array dynamic_states = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor,
  };

  vk::PipelineDynamicStateCreateInfo dynamic_state{};
  dynamic_state.setDynamicStates(dynamic_states);

  vk::GraphicsPipelineCreateInfo create_info{};
  create_info.pNext = &rendering_info;
  create_info.setStages(shader_stages);
  create_info.pVertexInputState = &vertex_input_state;
  create_info.pInputAssemblyState = &input_assembly_state;
  create_info.pViewportState = &viewport_state;
  create_info.pRasterizationState = &rasterization_state;
  create_info.pMultisampleState = &multisample_state;
  create_info.pDepthStencilState = &depth_stencil_state;
  create_info.pColorBlendState = &color_blend_state;
  create_info.pDynamicState = &dynamic_state;
  create_info.layout = *layout_;

  const vk::Optional<const vk::raii::PipelineCache> pipeline_cache = nullptr;
  handle_ = vk::raii::Pipeline{device, pipeline_cache, create_info};
}

const vk::raii::Pipeline& graphics_pipeline::handle() const noexcept { return handle_; }

const vk::raii::PipelineLayout& graphics_pipeline::layout() const noexcept { return layout_; }

vk::Format graphics_pipeline::color_format() const noexcept { return color_format_; }

graphics_pipeline::operator bool() const noexcept { return static_cast<bool>(*handle_); }

}  // namespace rl::vulkan
