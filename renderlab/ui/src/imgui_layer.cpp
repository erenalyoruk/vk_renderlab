#include "ui/imgui_layer.hpp"

#include <imgui.h>

namespace rl::ui {

imgui_layer::imgui_layer() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& imgui_io = ImGui::GetIO();
  imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

imgui_layer::~imgui_layer() noexcept { ImGui::DestroyContext(); }

}  // namespace rl::ui
