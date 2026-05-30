#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "platform/platform_event.hpp"

namespace rl::platform {

enum class input_action_phase : std::uint8_t {
  pressed,
  released,
};

struct key_binding {
  std::string action;
  key key_code = key::unknown;
  key_modifiers modifiers{};
  bool consume = true;
  bool allow_repeat = false;
};

struct input_action_event {
  std::string action;
  input_action_phase phase = input_action_phase::pressed;
  key key_code = key::unknown;
  key_modifiers modifiers{};
  bool repeat = false;
};

struct input_action_layer {
  std::string name;
  bool enabled = true;
  std::vector<key_binding> key_bindings;
};

class input_action_map final {
 public:
  std::size_t push_layer(input_action_layer layer);
  void clear_layers() noexcept;

  [[nodiscard]] bool set_layer_enabled(std::string_view name, bool enabled) noexcept;
  [[nodiscard]] std::vector<input_action_event> translate(const platform_event& event) const;

 private:
  std::vector<input_action_layer> layers_;
};

[[nodiscard]] bool modifiers_match(key_modifiers actual, key_modifiers required) noexcept;

}  // namespace rl::platform
