#pragma once

#include "base/noncopyable.hpp"

namespace rl::ui {

class imgui_layer final : public noncopyable {
 public:
  imgui_layer();
  ~imgui_layer() noexcept;

  imgui_layer(imgui_layer& other) = delete;
  imgui_layer& operator=(imgui_layer& other) = delete;

  imgui_layer(imgui_layer&& other) noexcept = delete;
  imgui_layer& operator=(imgui_layer&& other) noexcept = delete;
};

}  // namespace rl::ui
