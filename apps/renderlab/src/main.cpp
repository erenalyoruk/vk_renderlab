#include <chrono>
#include <cstdlib>
#include <exception>
#include <thread>

#include <spdlog/spdlog.h>

#include "assets/file_io.hpp"
#include "base/log.hpp"
#include "platform/sdl_window.hpp"
#include "ui/imgui_layer.hpp"
#include "vk/vulkan_context.hpp"

namespace {
[[nodiscard]] rl::log::logger_config make_logger_config() {
  rl::log::logger_config config{};

  config.application_name = "renderlab";
  config.log_directory = "logs";
  config.enable_console_sink = true;
  config.enable_file_sink = true;
  config.enable_memory_sink = true;
  config.truncate_file = true;

#ifndef NDEBUG
  config.level = rl::log::log_level::trace;
#else
  config.level = rl::log::log_level::info;
#endif

  config.flush_level = rl::log::log_level::warn;

  // Keep sync logging during early renderer bring-up. When the frame loop gets noisy,
  // we can flip this to true and use the async queue.
  config.async = false;

  return config;
}
}  // namespace

int main() {
  rl::log::scoped_logger logging{make_logger_config()};

  try {
    RL_APP_INFO("starting vk_renderlab");
    RL_APP_INFO("log file: '{}'", rl::log::current_log_file().string());

    rl::platform::sdl_window window({
      .title = "Vulkan RenderLab",
      .width = 1280,
      .height = 720,
    });

    rl::vulkan::vulkan_context vulkan_context{window};
    rl::ui::imgui_layer imgui_layer;

    const auto vertex_shader_path = rl::assets::resolve_runfile("renderlab/shaders/triangle.vert.spv");
    const auto fragment_shader_path = rl::assets::resolve_runfile("renderlab/shaders/triangle.frag.spv");
    const auto vertex_shader = rl::assets::read_binary_file(vertex_shader_path);
    const auto fragment_shader = rl::assets::read_binary_file(fragment_shader_path);

    RL_SHADER_INFO("loaded Slang/SPIR-V vertex shader: '{}' ({} bytes)", vertex_shader_path.string(),
                   vertex_shader.size());
    RL_SHADER_INFO("loaded Slang/SPIR-V fragment shader: '{}' ({} bytes)", fragment_shader_path.string(),
                   fragment_shader.size());

    RL_APP_INFO("bootstrap complete");

    while (!window.poll_events()) {
      // Placeholder loop. The next milestone will acquire a swapchain image, record a
      // command buffer and present a clear color.
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    RL_APP_INFO("main loop exited");
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    RL_APP_CRITICAL("fatal error: {}", error.what());
  } catch (...) {
    RL_APP_CRITICAL("fatal error: unknown exception");
  }

  return EXIT_FAILURE;
}
