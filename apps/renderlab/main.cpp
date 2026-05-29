#include <chrono>
#include <cstdlib>
#include <exception>
#include <thread>

#include <spdlog/spdlog.h>

#include "renderlab/assets/file_io.hpp"
#include "renderlab/base/log.hpp"
#include "renderlab/platform/sdl_window.hpp"
#include "renderlab/ui/imgui_layer.hpp"
#include "renderlab/vulkan/vulkan_context.hpp"

int main() {
  rl::init_logging();

  try {
    rl::platform::sdl_window window({
      .title = "Vulkan RenderLab",
      .width = 1280,
      .height = 720,
    });

    rl::vulkan::vulkan_context vulkan_context(window);
    rl::ui::imgui_layer imgui_layer;

    const auto vertex_shader_path = rl::assets::resolve_runfile("renderlab/shaders/triangle.vert.spv");
    const auto fragment_shader_path = rl::assets::resolve_runfile("renderlab/shaders/triangle.frag.spv");
    const auto vertex_shader = rl::assets::read_binary_file(vertex_shader_path);
    const auto fragment_shader = rl::assets::read_binary_file(fragment_shader_path);

    spdlog::info("loaded Slang/SPIR-V shader: {} ({} bytes)", vertex_shader_path.string(), vertex_shader.size());
    spdlog::info("loaded Slang/SPIR-V shader: {} ({} bytes)", fragment_shader_path.string(), fragment_shader.size());
    spdlog::info("Vulkan RenderLab bootstrap complete");

    while (!window.poll_events()) {
      // Placeholder loop. The next milestone will acquire a swapchain image, record a
      // command buffer and present a clear color.
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    spdlog::critical("fatal error: {}", error.what());
  } catch (...) {
    spdlog::critical("fatal error: unknown exception");
  }

  return EXIT_FAILURE;
}
