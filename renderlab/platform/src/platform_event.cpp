#include "platform/platform_event.hpp"

#include <algorithm>
#include <cstddef>
#include <variant>

namespace rl::platform {
namespace {

[[nodiscard]] constexpr std::size_t key_index(key key_code) noexcept { return static_cast<std::size_t>(key_code); }

[[nodiscard]] constexpr std::size_t mouse_button_index(mouse_button button) noexcept {
  return static_cast<std::size_t>(button);
}

[[nodiscard]] constexpr bool valid_key(key key_code) noexcept {
  return key_code != key::unknown && key_index(key_code) < key_index(key::count);
}

[[nodiscard]] constexpr bool valid_mouse_button(mouse_button button) noexcept {
  return button != mouse_button::unknown && mouse_button_index(button) < mouse_button_index(mouse_button::count);
}

}  // namespace

void input_state::apply(const platform_event& event) noexcept {
  if (const auto* keyboard = std::get_if<keyboard_event>(&event.payload)) {
    modifiers_ = keyboard->modifiers;

    if (valid_key(keyboard->key_code)) {
      keys_down_.at(key_index(keyboard->key_code)) = event.type == platform_event_type::key_pressed;
    }
  }

  if (const auto* button = std::get_if<mouse_button_event>(&event.payload)) {
    mouse_position_ = button->position;

    if (valid_mouse_button(button->button)) {
      mouse_buttons_down_.at(mouse_button_index(button->button)) = event.type == platform_event_type::mouse_pressed;
    }
  }

  if (const auto* motion = std::get_if<mouse_motion_event>(&event.payload)) {
    mouse_position_ = motion->position;
  }

  if (const auto* wheel = std::get_if<mouse_wheel_event>(&event.payload)) {
    mouse_position_ = wheel->mouse_position;
  }
}

bool input_state::is_down(key key_code) const noexcept {
  if (!valid_key(key_code)) {
    return false;
  }

  return keys_down_.at(key_index(key_code));
}

bool input_state::is_down(mouse_button button) const noexcept {
  if (!valid_mouse_button(button)) {
    return false;
  }

  return mouse_buttons_down_.at(mouse_button_index(button));
}

point2d input_state::mouse_position() const noexcept { return mouse_position_; }

key_modifiers input_state::modifiers() const noexcept { return modifiers_; }

bool is_close_requested(const std::vector<platform_event>& events) noexcept {
  return std::ranges::any_of(events.begin(), events.end(), [](const platform_event& event) {
    return event.type == platform_event_type::close_requested;
  });

  return false;
}

}  // namespace rl::platform
