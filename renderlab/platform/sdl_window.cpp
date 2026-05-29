#include "renderlab/platform/sdl_window.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_vulkan.h>

namespace rl::platform {
using std::vector;

namespace {
[[nodiscard]] std::runtime_error make_sdl_error(std::string_view what) {
  return std::runtime_error(std::string(what) + ": " + SDL_GetError());
}
}  // namespace

sdl_window::sdl_window(const window_config& config) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw make_sdl_error("SDL_Init(SDL_INIT_VIDEO) failed");
  }
  sdl_initialized_ = true;

  window_ =
      SDL_CreateWindow(config.title.c_str(), config.width, config.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  if (window_ == nullptr) {
    throw make_sdl_error("SDL_CreateWindow failed");
  }
}

sdl_window::~sdl_window() noexcept {
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  if (sdl_initialized_) {
    SDL_Quit();
    sdl_initialized_ = false;
  }
}

bool sdl_window::poll_events() const {
  (void)this;

  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
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
  return {extensions.begin(), extensions.end()};
}

VkSurfaceKHR sdl_window::create_vulkan_surface(VkInstance instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface)) {
    throw make_sdl_error("SDL_Vulkan_CreateSurface failed");
  }

  return surface;
}
}  // namespace rl::platform
