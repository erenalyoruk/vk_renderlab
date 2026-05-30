#include "platform/input_action.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rl::platform {
namespace {

[[nodiscard]] input_action_phase action_phase_for(platform_event_type type) noexcept {
  return type == platform_event_type::key_released ? input_action_phase::released : input_action_phase::pressed;
}

[[nodiscard]] bool binding_matches(const key_binding& binding, const keyboard_event& keyboard,
                                   platform_event_type type) noexcept {
  if (type != platform_event_type::key_pressed && type != platform_event_type::key_released) {
    return false;
  }

  if (binding.key_code != keyboard.key_code) {
    return false;
  }

  if (keyboard.repeat && !binding.allow_repeat) {
    return false;
  }

  return modifiers_match(keyboard.modifiers, binding.modifiers);
}

}  // namespace

std::size_t input_action_map::push_layer(input_action_layer layer) {
  layers_.push_back(std::move(layer));
  return layers_.size() - std::size_t{1};
}

void input_action_map::clear_layers() noexcept { layers_.clear(); }

bool input_action_map::set_layer_enabled(std::string_view name, bool enabled) noexcept {
  const auto layer = std::ranges::find_if(
      layers_, [name](const input_action_layer& current_layer) { return current_layer.name == name; });

  if (layer == layers_.end()) {
    return false;
  }

  layer->enabled = enabled;
  return true;
}

std::vector<input_action_event> input_action_map::translate(const platform_event& event) const {
  const auto* keyboard = std::get_if<keyboard_event>(&event.payload);

  if (keyboard == nullptr) {
    return {};
  }

  std::vector<input_action_event> actions;

  for (const input_action_layer& layer : std::views::reverse(layers_)) {
    if (!layer.enabled) {
      continue;
    }

    bool consumed = false;

    for (const key_binding& binding : layer.key_bindings) {
      if (!binding_matches(binding, *keyboard, event.type)) {
        continue;
      }

      actions.push_back(input_action_event{
        .action = binding.action,
        .phase = action_phase_for(event.type),
        .key_code = keyboard->key_code,
        .modifiers = keyboard->modifiers,
        .repeat = keyboard->repeat,
      });

      consumed = consumed || binding.consume;
    }

    if (consumed) {
      break;
    }
  }

  return actions;
}

bool modifiers_match(key_modifiers actual, key_modifiers required) noexcept {
  if (required.shift && !actual.shift) {
    return false;
  }

  if (required.ctrl && !actual.ctrl) {
    return false;
  }

  if (required.alt && !actual.alt) {
    return false;
  }

  if (required.super && !actual.super) {
    return false;
  }

  if (required.caps_lock && !actual.caps_lock) {
    return false;
  }

  if (required.num_lock && !actual.num_lock) {
    return false;
  }

  return true;
}

}  // namespace rl::platform
