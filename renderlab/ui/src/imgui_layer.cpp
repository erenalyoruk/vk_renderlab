#include "ui/imgui_layer.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include "platform/sdl_window.hpp"

namespace rl::ui {
namespace {

[[nodiscard]] void* create_imgui_context() {
  IMGUI_CHECKVERSION();
  return ImGui::CreateContext();
}

[[nodiscard]] ImGuiContext* imgui_context(void* context) noexcept { return static_cast<ImGuiContext*>(context); }

void build_font_atlas() {
  ImGuiIO& imgui_io = ImGui::GetIO();
  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  imgui_io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
}

}  // namespace

imgui_layer::imgui_layer(rl::platform::sdl_window& window) : context_{create_imgui_context()} {
  ImGui::SetCurrentContext(imgui_context(context_));

  ImGuiIO& imgui_io = ImGui::GetIO();
  imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui_ImplSDL3_InitForVulkan(window.native_handle());
  build_font_atlas();
}

imgui_layer::~imgui_layer() noexcept {
  ImGui::SetCurrentContext(imgui_context(context_));
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(imgui_context(context_));
}

void imgui_layer::handle_event(const SDL_Event& event) noexcept {
  ImGui::SetCurrentContext(imgui_context(context_));
  ImGui_ImplSDL3_ProcessEvent(&event);
}

void imgui_layer::begin_frame() {
  ImGui::SetCurrentContext(imgui_context(context_));
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void imgui_layer::end_frame() {
  ImGui::SetCurrentContext(imgui_context(context_));
  ImGui::Render();
}

}  // namespace rl::ui
