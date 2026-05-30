#include "platform/sdl_window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "base/log.hpp"
#include "platform/platform_event.hpp"

namespace rl::platform {

namespace {

constexpr std::string_view k_app_name = "Vulkan RenderLab";
constexpr std::string_view k_app_version = "0.1.0";
constexpr std::string_view k_app_identifier = "dev.eren.vulkan-renderlab";
constexpr std::int32_t k_icon_size = 64;

[[nodiscard]] std::runtime_error make_sdl_error(std::string_view what) {
  return std::runtime_error{std::string{what} + ": " + SDL_GetError()};
}

struct sdl_surface_deleter {
  void operator()(SDL_Surface* surface) const noexcept {
    if (surface != nullptr) {
      SDL_DestroySurface(surface);
    }
  }
};

using sdl_surface_ptr = std::unique_ptr<SDL_Surface, sdl_surface_deleter>;

void set_sdl_metadata() {
  const std::string app_name{k_app_name};
  const std::string app_version{k_app_version};
  const std::string app_identifier{k_app_identifier};

  if (!SDL_SetAppMetadata(app_name.c_str(), app_version.c_str(), app_identifier.c_str())) {
    throw make_sdl_error("SDL_SetAppMetadata failed");
  }

  if (!SDL_SetHint(SDL_HINT_APP_ID, app_identifier.c_str())) {
    throw make_sdl_error("SDL_SetHint(SDL_HINT_APP_ID) failed");
  }

  if (!SDL_SetHint(SDL_HINT_APP_NAME, app_name.c_str())) {
    throw make_sdl_error("SDL_SetHint(SDL_HINT_APP_NAME) failed");
  }
}

[[nodiscard]] sdl_surface_ptr make_window_icon() {
  sdl_surface_ptr icon{SDL_CreateSurface(k_icon_size, k_icon_size, SDL_PIXELFORMAT_RGBA32)};

  if (!icon) {
    throw make_sdl_error("SDL_CreateSurface failed");
  }

  struct icon_color {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;
  };

  constexpr std::array<icon_color, 4> palette = {
    icon_color{.red = 0xd0, .green = 0x8a, .blue = 0x38},
    icon_color{.red = 0x38, .green = 0xa3, .blue = 0xd0},
    icon_color{.red = 0x20, .green = 0x24, .blue = 0x2c},
    icon_color{.red = 0xff, .green = 0xff, .blue = 0xff},
  };

  for (std::int32_t row = 0; row < k_icon_size; ++row) {
    for (std::int32_t column = 0; column < k_icon_size; ++column) {
      const bool in_border = column < 4 || row < 4 || column >= k_icon_size - 4 || row >= k_icon_size - 4;
      const bool in_slash = column > row - 7 && column < row + 7;
      const bool in_backslash = column > (k_icon_size - row) - 11 && column < (k_icon_size - row) + 3;

      icon_color color = palette.at(2);
      if (in_border) {
        color = palette.at(0);
      } else if (in_slash) {
        color = palette.at(1);
      } else if (in_backslash) {
        color = palette.at(3);
      }

      if (!SDL_WriteSurfacePixel(icon.get(), column, row, color.red, color.green, color.blue, color.alpha)) {
        throw make_sdl_error("SDL_WriteSurfacePixel failed");
      }
    }
  }

  return icon;
}

void set_window_icon(SDL_Window* window) {
  const sdl_surface_ptr icon = make_window_icon();

  if (!SDL_SetWindowIcon(window, icon.get())) {
    throw make_sdl_error("SDL_SetWindowIcon failed");
  }
}

[[nodiscard]] key map_key(SDL_Scancode scancode) noexcept {
  const auto code = static_cast<int>(scancode);

  if (code >= static_cast<int>(SDL_SCANCODE_A) && code <= static_cast<int>(SDL_SCANCODE_Z)) {
    return static_cast<key>(static_cast<int>(key::a) + (code - static_cast<int>(SDL_SCANCODE_A)));
  }

  if (code >= static_cast<int>(SDL_SCANCODE_1) && code <= static_cast<int>(SDL_SCANCODE_9)) {
    return static_cast<key>(static_cast<int>(key::num_1) + (code - static_cast<int>(SDL_SCANCODE_1)));
  }

  if (scancode == SDL_SCANCODE_0) {
    return key::num_0;
  }

  switch (scancode) {
    case SDL_SCANCODE_ESCAPE:
      return key::escape;
    case SDL_SCANCODE_RETURN:
      return key::enter;
    case SDL_SCANCODE_TAB:
      return key::tab;
    case SDL_SCANCODE_SPACE:
      return key::space;
    case SDL_SCANCODE_BACKSPACE:
      return key::backspace;
    case SDL_SCANCODE_INSERT:
      return key::insert;
    case SDL_SCANCODE_DELETE:
      return key::delete_key;
    case SDL_SCANCODE_HOME:
      return key::home;
    case SDL_SCANCODE_END:
      return key::end;
    case SDL_SCANCODE_PAGEUP:
      return key::page_up;
    case SDL_SCANCODE_PAGEDOWN:
      return key::page_down;
    case SDL_SCANCODE_LEFT:
      return key::left;
    case SDL_SCANCODE_RIGHT:
      return key::right;
    case SDL_SCANCODE_UP:
      return key::up;
    case SDL_SCANCODE_DOWN:
      return key::down;
    case SDL_SCANCODE_F1:
      return key::f1;
    case SDL_SCANCODE_F2:
      return key::f2;
    case SDL_SCANCODE_F3:
      return key::f3;
    case SDL_SCANCODE_F4:
      return key::f4;
    case SDL_SCANCODE_F5:
      return key::f5;
    case SDL_SCANCODE_F6:
      return key::f6;
    case SDL_SCANCODE_F7:
      return key::f7;
    case SDL_SCANCODE_F8:
      return key::f8;
    case SDL_SCANCODE_F9:
      return key::f9;
    case SDL_SCANCODE_F10:
      return key::f10;
    case SDL_SCANCODE_F11:
      return key::f11;
    case SDL_SCANCODE_F12:
      return key::f12;
    case SDL_SCANCODE_LSHIFT:
      return key::left_shift;
    case SDL_SCANCODE_RSHIFT:
      return key::right_shift;
    case SDL_SCANCODE_LCTRL:
      return key::left_ctrl;
    case SDL_SCANCODE_RCTRL:
      return key::right_ctrl;
    case SDL_SCANCODE_LALT:
      return key::left_alt;
    case SDL_SCANCODE_RALT:
      return key::right_alt;
    case SDL_SCANCODE_LGUI:
      return key::left_super;
    case SDL_SCANCODE_RGUI:
      return key::right_super;
    default:
      return key::unknown;
  }
}

[[nodiscard]] mouse_button map_mouse_button(Uint8 button) noexcept {
  switch (button) {
    case SDL_BUTTON_LEFT:
      return mouse_button::left;
    case SDL_BUTTON_MIDDLE:
      return mouse_button::middle;
    case SDL_BUTTON_RIGHT:
      return mouse_button::right;
    case SDL_BUTTON_X1:
      return mouse_button::x1;
    case SDL_BUTTON_X2:
      return mouse_button::x2;
    default:
      return mouse_button::unknown;
  }
}

[[nodiscard]] key_modifiers map_modifiers(SDL_Keymod modifiers) noexcept {
  return {
    .shift = (modifiers & SDL_KMOD_SHIFT) != 0u,
    .ctrl = (modifiers & SDL_KMOD_CTRL) != 0u,
    .alt = (modifiers & SDL_KMOD_ALT) != 0u,
    .super = (modifiers & SDL_KMOD_GUI) != 0u,
    .caps_lock = (modifiers & SDL_KMOD_CAPS) != 0u,
    .num_lock = (modifiers & SDL_KMOD_NUM) != 0u,
  };
}

[[nodiscard]] bool event_belongs_to_window(SDL_WindowID event_window_id, SDL_WindowID window_id) noexcept {
  return event_window_id == 0 || event_window_id == window_id;
}

std::optional<platform_event> translate_empty_window_event(const SDL_WindowEvent& event, SDL_WindowID window_id,
                                                           platform_event_type type) {
  if (!event_belongs_to_window(event.windowID, window_id)) {
    return std::nullopt;
  }

  return platform_event{
    .type = type,
    .timestamp_ns = event.timestamp,
    .payload = {},
  };
}

std::optional<platform_event> translate_window_extent_event(const SDL_WindowEvent& event, SDL_WindowID window_id,
                                                            platform_event_type type) {
  if (!event_belongs_to_window(event.windowID, window_id)) {
    return std::nullopt;
  }

  return platform_event{
    .type = type,
    .timestamp_ns = event.timestamp,
    .payload =
        window_extent_event{
          .size =
              {
                .width = event.data1,
                .height = event.data2,
              },
        },
  };
}

std::optional<platform_event> translate_keyboard_event(const SDL_KeyboardEvent& event, SDL_WindowID window_id) {
  if (!event_belongs_to_window(event.windowID, window_id)) {
    return std::nullopt;
  }

  return platform_event{
    .type = event.down ? platform_event_type::key_pressed : platform_event_type::key_released,
    .timestamp_ns = event.timestamp,
    .payload =
        keyboard_event{
          .key_code = map_key(event.scancode),
          .modifiers = map_modifiers(event.mod),
          .repeat = event.repeat,
        },
  };
}

std::optional<platform_event> translate_mouse_motion_event(const SDL_MouseMotionEvent& event, SDL_WindowID window_id) {
  if (!event_belongs_to_window(event.windowID, window_id)) {
    return std::nullopt;
  }

  return platform_event{
    .type = platform_event_type::mouse_moved,
    .timestamp_ns = event.timestamp,
    .payload =
        mouse_motion_event{
          .position =
              {
                .x = event.x,
                .y = event.y,
              },
          .delta =
              {
                .x = event.xrel,
                .y = event.yrel,
              },
        },
  };
}

std::optional<platform_event> translate_mouse_button_event(const SDL_MouseButtonEvent& event, SDL_WindowID window_id) {
  if (!event_belongs_to_window(event.windowID, window_id)) {
    return std::nullopt;
  }

  return platform_event{
    .type = event.down ? platform_event_type::mouse_pressed : platform_event_type::mouse_released,
    .timestamp_ns = event.timestamp,
    .payload =
        mouse_button_event{
          .button = map_mouse_button(event.button),
          .position =
              {
                .x = event.x,
                .y = event.y,
              },
          .clicks = event.clicks,
        },
  };
}

std::optional<platform_event> translate_mouse_wheel_event(const SDL_MouseWheelEvent& event, SDL_WindowID window_id) {
  if (!event_belongs_to_window(event.windowID, window_id)) {
    return std::nullopt;
  }

  const float wheel_direction = event.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;

  return platform_event{
    .type = platform_event_type::mouse_wheel,
    .timestamp_ns = event.timestamp,
    .payload =
        mouse_wheel_event{
          .amount =
              {
                .x = event.x * wheel_direction,
                .y = event.y * wheel_direction,
              },
          .mouse_position =
              {
                .x = event.mouse_x,
                .y = event.mouse_y,
              },
        },
  };
}

[[nodiscard]] std::optional<platform_event> translate_sdl_event(const SDL_Event& event, SDL_WindowID window_id) {
  switch (event.type) {
    case SDL_EVENT_QUIT:
      return platform_event{
        .type = platform_event_type::close_requested,
        .timestamp_ns = event.quit.timestamp,
        .payload = {},
      };

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      return translate_empty_window_event(event.window, window_id, platform_event_type::close_requested);

    case SDL_EVENT_WINDOW_RESIZED:
      return translate_window_extent_event(event.window, window_id, platform_event_type::window_resized);

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      return translate_window_extent_event(event.window, window_id, platform_event_type::drawable_resized);

    case SDL_EVENT_WINDOW_SHOWN:
      return translate_empty_window_event(event.window, window_id, platform_event_type::window_shown);

    case SDL_EVENT_WINDOW_HIDDEN:
      return translate_empty_window_event(event.window, window_id, platform_event_type::window_hidden);

    case SDL_EVENT_WINDOW_MINIMIZED:
      return translate_empty_window_event(event.window, window_id, platform_event_type::window_minimized);

    case SDL_EVENT_WINDOW_RESTORED:
      return translate_empty_window_event(event.window, window_id, platform_event_type::window_restored);

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      return translate_empty_window_event(event.window, window_id, platform_event_type::focus_gained);

    case SDL_EVENT_WINDOW_FOCUS_LOST:
      return translate_empty_window_event(event.window, window_id, platform_event_type::focus_lost);

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      return translate_keyboard_event(event.key, window_id);

    case SDL_EVENT_MOUSE_MOTION:
      return translate_mouse_motion_event(event.motion, window_id);

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      return translate_mouse_button_event(event.button, window_id);

    case SDL_EVENT_MOUSE_WHEEL:
      return translate_mouse_wheel_event(event.wheel, window_id);

    default:
      return std::nullopt;
  }
}

}  // namespace

sdl_video_session::sdl_video_session() {
  set_sdl_metadata();

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw make_sdl_error("SDL_Init(SDL_INIT_VIDEO) failed");
  }

  RL_PLATFORM_DEBUG("SDL3 video subsystem initialized");
}

sdl_video_session::~sdl_video_session() noexcept {
  RL_PLATFORM_DEBUG("shutting down SDL3");
  SDL_Quit();
}

void sdl_window_deleter::operator()(SDL_Window* window) const noexcept {
  if (window == nullptr) {
    return;
  }

  RL_PLATFORM_DEBUG("destroying SDL3 window");
  SDL_DestroyWindow(window);
}

sdl_window::sdl_window(const window_config& config) {
  RL_PLATFORM_INFO("creating SDL3 window: title='{}', width={}, height={}", config.title, config.width, config.height);

  SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;

  if (!config.visible) {
    flags |= SDL_WINDOW_HIDDEN;
  }

  window_.reset(SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags));

  if (!window_) {
    throw make_sdl_error("SDL_CreateWindow failed");
  }

  window_id_ = SDL_GetWindowID(window_.get());
  set_window_icon(window_.get());
  refresh_window_state();

  RL_PLATFORM_INFO("SDL3 window created");
}

std::vector<platform_event> sdl_window::poll_events() {
  std::vector<platform_event> events;

  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    std::optional<platform_event> platform_event = translate_sdl_event(event, window_id_);

    if (platform_event.has_value()) {
      apply_event(*platform_event);
      events.push_back(*platform_event);
    }
  }

  return events;
}

void sdl_window::show() {
  if (!SDL_ShowWindow(window_.get())) {
    throw make_sdl_error("SDL_ShowWindow failed");
  }

  refresh_window_state();
}

void sdl_window::hide() {
  if (!SDL_HideWindow(window_.get())) {
    throw make_sdl_error("SDL_HideWindow failed");
  }

  refresh_window_state();
  RL_PLATFORM_INFO("SDL3 window hidden");
}

const window_state& sdl_window::state() const noexcept { return state_; }

SDL_Window* sdl_window::native_handle() const noexcept { return window_.get(); }

std::vector<std::string> sdl_window::required_vulkan_extensions() {
  Uint32 extension_count = 0;
  const char* const* extension_names = SDL_Vulkan_GetInstanceExtensions(&extension_count);

  if (extension_names == nullptr) {
    throw make_sdl_error("SDL_Vulkan_GetInstanceExtensions failed");
  }

  auto extensions = std::span{extension_names, extension_count};

  RL_PLATFORM_DEBUG("SDL3 requires {} Vulkan instance extension(s)", extension_count);

  Uint32 index = 0;
  for (const char* extension : extensions) {
    RL_PLATFORM_TRACE("required Vulkan extension[{}]: {}", index++, extension);
  }

  std::vector<std::string> result;
  result.reserve(extensions.size());

  for (const char* extension : extensions) {
    result.emplace_back(extension);
  }

  return result;
}

VkSurfaceKHR sdl_window::create_vulkan_surface(VkInstance instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window_.get(), instance, nullptr, &surface)) {
    throw make_sdl_error("SDL_Vulkan_CreateSurface failed");
  }

  RL_PLATFORM_DEBUG("created SDL3 Vulkan surface");
  return surface;
}

void sdl_window::refresh_window_state() {
  int width = 0;
  int height = 0;
  int drawable_width = 0;
  int drawable_height = 0;

  if (!SDL_GetWindowSize(window_.get(), &width, &height)) {
    throw make_sdl_error("SDL_GetWindowSize failed");
  }

  if (!SDL_GetWindowSizeInPixels(window_.get(), &drawable_width, &drawable_height)) {
    throw make_sdl_error("SDL_GetWindowSizeInPixels failed");
  }

  const SDL_WindowFlags flags = SDL_GetWindowFlags(window_.get());

  state_.size = {
    .width = width,
    .height = height,
  };
  state_.drawable_size = {
    .width = drawable_width,
    .height = drawable_height,
  };
  state_.visible = !static_cast<bool>(flags & SDL_WINDOW_HIDDEN);
  state_.minimized = static_cast<bool>(flags & SDL_WINDOW_MINIMIZED);
  state_.focused = static_cast<bool>(flags & SDL_WINDOW_INPUT_FOCUS);
}

void sdl_window::apply_event(const platform_event& event) noexcept {
  switch (event.type) {
    case platform_event_type::close_requested:
      state_.close_requested = true;
      RL_PLATFORM_INFO("window close requested");
      break;
    case platform_event_type::window_resized:
      if (const auto* resize = std::get_if<window_extent_event>(&event.payload)) {
        state_.size = resize->size;
        RL_PLATFORM_DEBUG("window resized: {}x{}", state_.size.width, state_.size.height);
      }
      break;
    case platform_event_type::drawable_resized:
      if (const auto* resize = std::get_if<window_extent_event>(&event.payload)) {
        state_.drawable_size = resize->size;
        RL_PLATFORM_DEBUG("window drawable resized: {}x{}", state_.drawable_size.width, state_.drawable_size.height);
      }
      break;
    case platform_event_type::window_shown:
      state_.visible = true;
      RL_PLATFORM_DEBUG("window shown");
      break;
    case platform_event_type::window_hidden:
      state_.visible = false;
      RL_PLATFORM_DEBUG("window hidden");
      break;
    case platform_event_type::window_minimized:
      state_.minimized = true;
      RL_PLATFORM_DEBUG("window minimized");
      break;
    case platform_event_type::window_restored:
      state_.minimized = false;
      RL_PLATFORM_DEBUG("window restored");
      break;
    case platform_event_type::focus_gained:
      state_.focused = true;
      RL_PLATFORM_DEBUG("window focus gained");
      break;
    case platform_event_type::focus_lost:
      state_.focused = false;
      RL_PLATFORM_DEBUG("window focus lost");
      break;
    default:
      break;
  }
}

}  // namespace rl::platform
