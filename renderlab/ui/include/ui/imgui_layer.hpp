#pragma once

#include <SDL3/SDL_events.h>

#include "base/noncopyable.hpp"

namespace rl::platform {
class sdl_window;
}

namespace rl::ui {

class imgui_layer final : public noncopyable {
 public:
  explicit imgui_layer(rl::platform::sdl_window& window);
  ~imgui_layer() noexcept;

  imgui_layer(imgui_layer& other) = delete;
  imgui_layer& operator=(imgui_layer& other) = delete;

  imgui_layer(imgui_layer&& other) noexcept = delete;
  imgui_layer& operator=(imgui_layer&& other) noexcept = delete;

  void handle_event(const SDL_Event& event) noexcept;
  void begin_frame();
  void end_frame();

 private:
  void* context_ = nullptr;
};

}  // namespace rl::ui
