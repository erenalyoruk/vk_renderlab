#include "ui/imgui_layer.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <spdlog/fmt/fmt.h>
#include <vulkan/vulkan.h>

#include "base/log.hpp"
#include "platform/sdl_window.hpp"
#include "vk/renderer.hpp"
#include "vk/vulkan_context.hpp"

namespace rl::ui {
namespace {

[[nodiscard]] void* create_imgui_context() {
  IMGUI_CHECKVERSION();
  return ImGui::CreateContext();
}

[[nodiscard]] ImGuiContext* imgui_context(void* context) noexcept { return static_cast<ImGuiContext*>(context); }

void check_vk_result(VkResult result) {
  if (result == VK_SUCCESS) {
    return;
  }

  RL_UI_ERROR("ImGui Vulkan backend error: VkResult={}", static_cast<int>(result));
}

void text_unformatted(const std::string& text) { ImGui::TextUnformatted(text.c_str()); }

void draw_render_path_control(rl::vulkan::renderer_settings& settings) {
  constexpr std::array render_paths{
    rl::vulkan::render_path::forward,
    rl::vulkan::render_path::forward_plus,
    rl::vulkan::render_path::deferred,
  };

  const std::string current_path{rl::vulkan::to_string(settings.path)};
  if (!ImGui::BeginCombo("Path", current_path.c_str())) {
    return;
  }

  for (const rl::vulkan::render_path path : render_paths) {
    const std::string path_name{rl::vulkan::to_string(path)};
    const bool selected = settings.path == path;
    if (ImGui::Selectable(path_name.c_str(), selected)) {
      settings.path = path;
    }
    if (selected) {
      ImGui::SetItemDefaultFocus();
    }
  }

  ImGui::EndCombo();
}

void draw_renderer_settings(rl::vulkan::renderer_settings& settings) {
  draw_render_path_control(settings);

  std::array clear_color = settings.clear_color;
  if (ImGui::ColorEdit4("Clear", clear_color.data())) {
    settings.clear_color = clear_color;
  }

  ImGui::Checkbox("Recreate on suboptimal swapchain", &settings.recreate_swapchain_on_suboptimal);
}

void draw_present_mode_control(rl::vulkan::renderer& renderer) {
  constexpr std::array present_modes{
    vk::PresentModeKHR::eFifo,
    vk::PresentModeKHR::eMailbox,
    vk::PresentModeKHR::eImmediate,
    vk::PresentModeKHR::eFifoRelaxed,
  };

  const rl::vulkan::renderer_settings& settings = renderer.settings();
  const std::string current_present_mode = vk::to_string(settings.preferred_present_mode);
  if (!ImGui::BeginCombo("Preferred present mode", current_present_mode.c_str())) {
    return;
  }

  for (const vk::PresentModeKHR present_mode : present_modes) {
    const std::string present_mode_name = vk::to_string(present_mode);
    const bool selected = settings.preferred_present_mode == present_mode;
    if (ImGui::Selectable(present_mode_name.c_str(), selected)) {
      renderer.set_preferred_present_mode(present_mode);
    }
    if (selected) {
      ImGui::SetItemDefaultFocus();
    }
  }

  ImGui::EndCombo();
}

void draw_frame_count_control(rl::vulkan::renderer& renderer) {
  constexpr int min_frames_in_flight = 1;
  constexpr int max_frames_in_flight = 3;

  int frames_in_flight = static_cast<int>(renderer.settings().max_frames_in_flight);
  if (ImGui::SliderInt("Frames in flight", &frames_in_flight, min_frames_in_flight, max_frames_in_flight)) {
    renderer.set_max_frames_in_flight(static_cast<std::uint32_t>(frames_in_flight));
  }
}

}  // namespace

imgui_layer::imgui_layer(rl::platform::sdl_window& window, const rl::vulkan::vulkan_context& context,
                         imgui_render_target render_target)
    : context_{create_imgui_context()}, device_{context.c_device()}, render_target_{render_target} {
  ImGui::SetCurrentContext(imgui_context(context_));

  ImGuiIO& imgui_io = ImGui::GetIO();
  imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui_ImplSDL3_InitForVulkan(window.native_handle());

  VkPipelineRenderingCreateInfo pipeline_rendering_info{};
  pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  pipeline_rendering_info.colorAttachmentCount = 1;
  pipeline_rendering_info.pColorAttachmentFormats = &render_target.color_format;

  const std::optional<std::uint32_t> graphics_queue_family = context.device().queues().graphics;
  if (!graphics_queue_family.has_value()) {
    throw std::runtime_error{"ImGui Vulkan backend requires a graphics queue"};
  }

  ImGui_ImplVulkan_InitInfo init_info{};
  init_info.ApiVersion = VK_API_VERSION_1_3;
  init_info.Instance = context.c_instance();
  init_info.PhysicalDevice = context.c_physical_device();
  init_info.Device = device_;
  init_info.QueueFamily = *graphics_queue_family;
  init_info.Queue = context.device().c_graphics_queue();
  init_info.DescriptorPoolSize = 32;
  init_info.MinImageCount = render_target.min_image_count;
  init_info.ImageCount = render_target.image_count;
  init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_info;
  init_info.UseDynamicRendering = true;
  init_info.CheckVkResultFn = check_vk_result;

  if (!ImGui_ImplVulkan_Init(&init_info)) {
    throw std::runtime_error{"failed to initialize ImGui Vulkan backend"};
  }

  RL_UI_INFO("ImGui Vulkan backend ready: images={}, min_images={}", render_target.image_count,
             render_target.min_image_count);
}

imgui_layer::~imgui_layer() noexcept {
  ImGui::SetCurrentContext(imgui_context(context_));
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(imgui_context(context_));
}

void imgui_layer::handle_event(const SDL_Event& event) noexcept {
  ImGui::SetCurrentContext(imgui_context(context_));
  ImGui_ImplSDL3_ProcessEvent(&event);
}

void imgui_layer::update_render_target(imgui_render_target render_target) {
  if (render_target.generation == render_target_.generation) {
    return;
  }

  if (render_target.color_format != render_target_.color_format) {
    RL_UI_WARN("ImGui swapchain color format changed; keeping existing pipeline format");
  }

  if (render_target.min_image_count != render_target_.min_image_count) {
    ImGui::SetCurrentContext(imgui_context(context_));
    ImGui_ImplVulkan_SetMinImageCount(render_target.min_image_count);
  }

  render_target_ = render_target;
}

void imgui_layer::begin_frame() {
  ImGui::SetCurrentContext(imgui_context(context_));
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void imgui_layer::draw_renderer_panel(const rl::vulkan::renderer_status& status, rl::vulkan::renderer& renderer) {
  ImGui::SetCurrentContext(imgui_context(context_));

  ImGui::SetNextWindowPos(ImVec2{12.0f, 12.0f}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2{340.0f, 0.0f}, ImGuiCond_FirstUseEver);

  if (ImGui::Begin("RenderLab")) {
    draw_renderer_settings(renderer.settings());
    draw_present_mode_control(renderer);
    draw_frame_count_control(renderer);

    ImGui::SeparatorText("Status");
    text_unformatted(fmt::format("Frame: {}", status.frame_index));
    text_unformatted(fmt::format("Frames in flight: {}", status.frames_in_flight));
    text_unformatted(fmt::format("Render path: {}", rl::vulkan::to_string(status.path)));
    text_unformatted(fmt::format("Present mode: {}", vk::to_string(status.present_mode)));
    text_unformatted(fmt::format("Swapchain: {}x{}, images={}", status.swapchain_extent.width,
                                 status.swapchain_extent.height, status.swapchain_image_count));
    text_unformatted(fmt::format("Swapchain generation: {}", status.swapchain_generation));
    text_unformatted(fmt::format("Frame graph passes: {}", status.frame_graph_pass_count));
    text_unformatted(fmt::format("Suspended: {}", status.suspended ? "yes" : "no"));
  }
  ImGui::End();
}

void imgui_layer::end_frame() {
  ImGui::SetCurrentContext(imgui_context(context_));
  ImGui::Render();
}

void imgui_layer::render(VkCommandBuffer command_buffer) {
  ImGui::SetCurrentContext(imgui_context(context_));
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
}

}  // namespace rl::ui
