#pragma once

#include "renderlab/base/noncopyable.hpp"

namespace rl::ui {
class imgui_layer final : public noncopyable {
 public:
  imgui_layer();
  ~imgui_layer() noexcept;
};
}  // namespace rl::ui
