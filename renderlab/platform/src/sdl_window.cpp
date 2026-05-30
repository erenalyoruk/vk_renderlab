#include "platform/sdl_window.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_vulkan.h>

#include "base/log.hpp"

namespace rl::platform {

namespace {
[[nodiscard]] std::runtime_error make_sdl_error(std::string_view what) {
  return std::runtime_error{std::string{what} + ": " + SDL_GetError()};
}
}  // namespace

sdl_window::sdl_window(const window_config& config) {
  RL_PLATFORM_INFO("creating SDL3 window: title='{}', width={}, height={}", config.title, config.width, config.height);

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw make_sdl_error("SDL_Init(SDL_INIT_VIDEO) failed");
  }
  sdl_initialized_ = true;

  window_ =
      SDL_CreateWindow(config.title.c_str(), config.width, config.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  if (window_ == nullptr) {
    throw make_sdl_error("SDL_CreateWindow failed");
  }

  RL_PLATFORM_INFO("SDL3 window created");
}

sdl_window::~sdl_window() noexcept {
  if (window_ != nullptr) {
    RL_PLATFORM_DEBUG("destroying SDL3 window");
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  if (sdl_initialized_) {
    RL_PLATFORM_DEBUG("shutting down SDL3");
    SDL_Quit();
    sdl_initialized_ = false;
  }
}

bool sdl_window::poll_events() const {
  (void)this;

  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
      RL_PLATFORM_INFO("SDL quit event received");
      return true;
    }
  }

  return false;
}

SDL_Window* sdl_window::native_handle() const noexcept { return window_; }

std::vector<const char*> sdl_window::required_vulkan_extensions() {
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

  return {extensions.begin(), extensions.end()};
}

VkSurfaceKHR sdl_window::create_vulkan_surface(VkInstance instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface)) {
    throw make_sdl_error("SDL_Vulkan_CreateSurface failed");
  }

  RL_PLATFORM_DEBUG("created SDL3 Vulkan surface");
  return surface;
}

}  // namespace rl::platform
