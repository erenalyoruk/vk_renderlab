#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace rl::platform {

struct extent2d {
  std::int32_t width = 0;
  std::int32_t height = 0;
};

struct point2d {
  float x = 0.0f;
  float y = 0.0f;
};

enum class platform_event_type : std::uint8_t {
  close_requested,
  window_resized,
  drawable_resized,
  window_minimized,
  window_restored,
  focus_gained,
  focus_lost,
  key_pressed,
  key_released,
  mouse_moved,
  mouse_pressed,
  mouse_released,
  mouse_wheel,
};

enum class key : std::uint8_t {
  unknown,

  a,
  b,
  c,
  d,
  e,
  f,
  g,
  h,
  i,
  j,
  k,
  l,
  m,
  n,
  o,
  p,
  q,
  r,
  s,
  t,
  u,
  v,
  w,
  x,
  y,
  z,

  num_0,
  num_1,
  num_2,
  num_3,
  num_4,
  num_5,
  num_6,
  num_7,
  num_8,
  num_9,

  escape,
  enter,
  tab,
  space,
  backspace,
  insert,
  delete_key,
  home,
  end,
  page_up,
  page_down,
  left,
  right,
  up,
  down,

  f1,
  f2,
  f3,
  f4,
  f5,
  f6,
  f7,
  f8,
  f9,
  f10,
  f11,
  f12,

  left_shift,
  right_shift,
  left_ctrl,
  right_ctrl,
  left_alt,
  right_alt,
  left_super,
  right_super,

  count,
};

enum class mouse_button : std::uint8_t {
  unknown,
  left,
  middle,
  right,
  x1,
  x2,
  count,
};

struct key_modifiers {
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool super = false;
  bool caps_lock = false;
  bool num_lock = false;
};

struct window_state {
  extent2d size{};
  extent2d drawable_size{};
  bool minimized = false;
  bool focused = false;
  bool close_requested = false;
};

struct window_extent_event {
  extent2d size{};
};

struct keyboard_event {
  key key_code = key::unknown;
  key_modifiers modifiers{};
  bool repeat = false;
};

struct mouse_motion_event {
  point2d position{};
  point2d delta{};
};

struct mouse_button_event {
  mouse_button button = mouse_button::unknown;
  point2d position{};
  std::uint8_t clicks = 0;
};

struct mouse_wheel_event {
  point2d amount{};
  point2d mouse_position{};
};

using platform_event_payload = std::variant<std::monostate, window_extent_event, keyboard_event, mouse_motion_event,
                                            mouse_button_event, mouse_wheel_event>;

struct platform_event {
  platform_event_type type = platform_event_type::close_requested;
  std::uint64_t timestamp_ns = 0;
  platform_event_payload payload;
};

class input_state {
 public:
  void apply(const platform_event& event) noexcept;

  [[nodiscard]] bool is_down(key key_code) const noexcept;
  [[nodiscard]] bool is_down(mouse_button button) const noexcept;

  [[nodiscard]] point2d mouse_position() const noexcept;
  [[nodiscard]] key_modifiers modifiers() const noexcept;

 private:
  std::array<bool, static_cast<std::size_t>(key::count)> keys_down_{};
  std::array<bool, static_cast<std::size_t>(mouse_button::count)> mouse_buttons_down_{};
  point2d mouse_position_{};
  key_modifiers modifiers_{};
};

[[nodiscard]] bool is_close_requested(const std::vector<platform_event>& events) noexcept;

}  // namespace rl::platform
