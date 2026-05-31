#include "vk/render_path.hpp"

#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "vk/frame_graph.hpp"
#include "vk/pipeline.hpp"

namespace rl::vulkan {
namespace {

[[nodiscard]] vk::ImageSubresourceRange color_subresource_range() noexcept {
  vk::ImageSubresourceRange result{};
  result.aspectMask = vk::ImageAspectFlagBits::eColor;
  result.baseMipLevel = 0;
  result.levelCount = 1;
  result.baseArrayLayer = 0;
  result.layerCount = 1;
  return result;
}

[[nodiscard]] vk::ImageSubresourceRange depth_subresource_range() noexcept {
  vk::ImageSubresourceRange result{};
  result.aspectMask = vk::ImageAspectFlagBits::eDepth;
  result.baseMipLevel = 0;
  result.levelCount = 1;
  result.baseArrayLayer = 0;
  result.layerCount = 1;
  return result;
}

[[nodiscard]] vk::ImageMemoryBarrier2 make_color_layout_barrier(
    vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout, vk::PipelineStageFlags2 src_stage,
    vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access) noexcept {
  vk::ImageMemoryBarrier2 barrier{};
  barrier.srcStageMask = src_stage;
  barrier.srcAccessMask = src_access;
  barrier.dstStageMask = dst_stage;
  barrier.dstAccessMask = dst_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.image = image;
  barrier.subresourceRange = color_subresource_range();
  return barrier;
}

[[nodiscard]] vk::ImageMemoryBarrier2 make_depth_layout_barrier(vk::Image image, vk::ImageLayout old_layout,
                                                                vk::ImageLayout new_layout) noexcept {
  vk::ImageMemoryBarrier2 barrier{};
  if (old_layout == vk::ImageLayout::eUndefined) {
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
    barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
  } else {
    barrier.srcStageMask =
        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
    barrier.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
  }
  barrier.dstStageMask =
      vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
  barrier.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.image = image;
  barrier.subresourceRange = depth_subresource_range();
  return barrier;
}

[[nodiscard]] std::string_view clear_pass_name(render_path path) noexcept {
  switch (path) {
    case render_path::forward:
      return "forward.clear_swapchain";
    case render_path::forward_plus:
      return "forward_plus.clear_swapchain";
    case render_path::deferred:
      return "deferred.clear_swapchain";
  }

  return "unknown.clear_swapchain";
}

void import_swapchain(frame_graph& graph) {
  graph.import_resource(frame_graph_resource{
    .name = "swapchain.color",
    .type = frame_graph_resource_type::swapchain_image,
  });
}

void import_depth_target(frame_graph& graph, const std::optional<render_path_depth_target>& depth) {
  if (!depth.has_value()) {
    return;
  }

  graph.import_resource(frame_graph_resource{
    .name = "scene.depth",
    .type = frame_graph_resource_type::depth_image,
  });
}

[[nodiscard]] std::vector<frame_graph_access> swapchain_pass_accesses(
    const std::optional<render_path_depth_target>& depth) {
  std::vector<frame_graph_access> accesses;
  accesses.push_back(frame_graph_access{
    .resource = "swapchain.color",
    .access = frame_graph_access_type::write,
  });

  if (depth.has_value()) {
    accesses.push_back(frame_graph_access{
      .resource = "scene.depth",
      .access = frame_graph_access_type::write,
    });
  }

  return accesses;
}

void add_swapchain_clear_pass(frame_graph& graph, const render_path_build_info& info) {
  const render_path_swapchain_target target = info.swapchain;
  const std::optional<render_path_depth_target> depth = info.depth;
  const std::optional<render_path_debug_draw> debug_draw = info.debug_draw;

  graph.add_pass(frame_graph_pass{
    .name = std::string{clear_pass_name(info.path)},
    .queue = frame_graph_queue::graphics,
    .accesses = swapchain_pass_accesses(depth),
    .execute =
        [target, depth, debug_draw](const frame_graph_context& graph_context) {
          const vk::raii::CommandBuffer& command_buffer = graph_context.command_buffer.get();

          const vk::ImageMemoryBarrier2 to_color_attachment = make_color_layout_barrier(
              target.image, target.old_layout, vk::ImageLayout::eColorAttachmentOptimal,
              vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone,
              vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);

          vk::DependencyInfo to_color_attachment_dependency{};
          to_color_attachment_dependency.setImageMemoryBarriers(to_color_attachment);
          command_buffer.pipelineBarrier2(to_color_attachment_dependency);

          if (depth.has_value()) {
            const vk::ImageMemoryBarrier2 to_depth_attachment =
                make_depth_layout_barrier(depth->image, depth->old_layout, vk::ImageLayout::eDepthAttachmentOptimal);

            vk::DependencyInfo to_depth_attachment_dependency{};
            to_depth_attachment_dependency.setImageMemoryBarriers(to_depth_attachment);
            command_buffer.pipelineBarrier2(to_depth_attachment_dependency);
          }

          vk::RenderingAttachmentInfo color_attachment{};
          color_attachment.imageView = target.image_view;
          color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
          color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
          color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
          color_attachment.clearValue = vk::ClearValue{target.clear_color};

          vk::RenderingAttachmentInfo depth_attachment{};
          if (depth.has_value()) {
            depth_attachment.imageView = depth->image_view;
            depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
            depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            depth_attachment.clearValue = vk::ClearValue{depth->clear_value};
          }

          vk::RenderingInfo rendering_info{};
          rendering_info.renderArea.offset = vk::Offset2D{0, 0};
          rendering_info.renderArea.extent = target.extent;
          rendering_info.layerCount = 1;
          rendering_info.setColorAttachments(color_attachment);
          if (depth.has_value()) {
            rendering_info.pDepthAttachment = &depth_attachment;
          }

          command_buffer.beginRendering(rendering_info);

          if (debug_draw.has_value() && debug_draw->pipeline != nullptr) {
            const graphics_pipeline& pipeline = *debug_draw->pipeline;
            const vk::Viewport viewport{
              0.0f, 0.0f, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0.0f, 1.0f,
            };
            const vk::Rect2D scissor{
              vk::Offset2D{0, 0},
              target.extent,
            };
            const std::array viewports = {viewport};
            const std::array scissors = {scissor};

            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.handle());
            command_buffer.setViewport(0, viewports);
            command_buffer.setScissor(0, scissors);
            const std::array vertex_buffers = {debug_draw->vertex_buffer};
            const std::array vertex_offsets = {vk::DeviceSize{0}};
            command_buffer.bindVertexBuffers(0, vertex_buffers, vertex_offsets);
            command_buffer.bindIndexBuffer(debug_draw->index_buffer, 0, vk::IndexType::eUint16);
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout(), 0,
                                              debug_draw->descriptor_set, nullptr);
            command_buffer.drawIndexed(debug_draw->index_count, 1, 0, 0, 0);
          }

          command_buffer.endRendering();

          const vk::ImageMemoryBarrier2 to_present = make_color_layout_barrier(
              target.image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
              vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
              vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone);

          vk::DependencyInfo to_present_dependency{};
          to_present_dependency.setImageMemoryBarriers(to_present);
          command_buffer.pipelineBarrier2(to_present_dependency);
        },
  });
}

}  // namespace

void build_render_path_frame_graph(frame_graph& graph, const render_path_build_info& info) {
  graph.clear();
  import_swapchain(graph);
  import_depth_target(graph, info.depth);

  switch (info.path) {
    case render_path::forward:
    case render_path::forward_plus:
    case render_path::deferred:
      add_swapchain_clear_pass(graph, info);
      break;
  }

  graph.compile();
}

std::string_view to_string(render_path path) noexcept {
  switch (path) {
    case render_path::forward:
      return "forward";
    case render_path::forward_plus:
      return "forward_plus";
    case render_path::deferred:
      return "deferred";
  }

  return "unknown";
}

}  // namespace rl::vulkan
