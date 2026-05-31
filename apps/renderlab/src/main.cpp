#include <chrono>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "base/log.hpp"
#include "base/log_level.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/input_action.hpp"
#include "platform/platform_event.hpp"
#include "platform/sdl_window.hpp"
#include "ui/imgui_layer.hpp"
#include "vk/renderer.hpp"
#include "vk/vulkan_context.hpp"

namespace {
constexpr std::string_view quit_action = "app.quit";
constexpr std::string_view save_scene_action = "scene.save";

[[nodiscard]] rl::log::logger_config make_logger_config() {
  rl::log::logger_config config;

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

void log_drawable_resize(const rl::platform::platform_event& event) {
  const auto* resize = std::get_if<rl::platform::window_extent_event>(&event.payload);
  if (resize == nullptr) {
    return;
  }

  RL_RENDER_INFO("drawable resize queued for swapchain: {}x{}", resize->size.width, resize->size.height);
}

void register_event_listeners(rl::platform::event_dispatcher& event_dispatcher, rl::vulkan::renderer& renderer) {
  event_dispatcher.subscribe(rl::platform::platform_event_type::drawable_resized, log_drawable_resize);
  event_dispatcher.subscribe_all(
      [&renderer](const rl::platform::platform_event& event) { renderer.handle_event(event); });
}

[[nodiscard]] rl::platform::input_action_map make_input_action_map() {
  rl::platform::input_action_map input_actions;

  input_actions.push_layer(rl::platform::input_action_layer{
    .name = "global",
    .enabled = true,
    .key_bindings =
        {
          rl::platform::key_binding{
            .action = std::string{quit_action},
            .key_code = rl::platform::key::escape,
          },
          rl::platform::key_binding{
            .action = std::string{save_scene_action},
            .key_code = rl::platform::key::s,
            .modifiers =
                {
                  .ctrl = true,
                },
          },
        },
  });

  return input_actions;
}

[[nodiscard]] bool action_pressed(const rl::platform::input_action_event& action, std::string_view action_name) {
  return action.action == action_name && action.phase == rl::platform::input_action_phase::pressed;
}

void process_action(const rl::platform::input_action_event& action, bool& running) {
  if (action_pressed(action, quit_action)) {
    running = false;
    return;
  }

  if (action_pressed(action, save_scene_action)) {
    RL_APP_INFO("scene save action triggered");
  }
}

void process_platform_events(const std::vector<rl::platform::platform_event>& events,
                             rl::platform::input_state& input_state,
                             const rl::platform::event_dispatcher& event_dispatcher,
                             const rl::platform::input_action_map& input_actions, bool& running) {
  for (const rl::platform::platform_event& event : events) {
    input_state.apply(event);
    event_dispatcher.dispatch(event);

    const std::vector<rl::platform::input_action_event> actions = input_actions.translate(event);
    for (const rl::platform::input_action_event& action : actions) {
      process_action(action, running);
    }
  }
}

void sleep_until_next_frame(bool renderer_suspended) {
  const auto sleep_duration = renderer_suspended ? std::chrono::milliseconds{32} : std::chrono::milliseconds{8};
  std::this_thread::sleep_for(sleep_duration);
}
}  // namespace

int main() {
  const rl::log::scoped_logger logging{make_logger_config()};

  try {
    RL_APP_INFO("starting vk_renderlab");
    RL_APP_INFO("log file: '{}'", rl::log::current_log_file().string());

    rl::platform::sdl_window window({
      .title = "Vulkan RenderLab",
      .width = 1280,
      .height = 720,
      .visible = true,
    });

    const rl::vulkan::vulkan_context vulkan_context{window};
    rl::vulkan::renderer renderer{vulkan_context, window.state().drawable_size};
    const rl::vulkan::renderer_ui_render_target renderer_ui_target = renderer.ui_render_target();
    rl::ui::imgui_layer imgui_layer{window, vulkan_context,
                                    rl::ui::imgui_render_target{
                                      .color_format = renderer_ui_target.color_format,
                                      .min_image_count = renderer_ui_target.min_image_count,
                                      .image_count = renderer_ui_target.image_count,
                                      .generation = renderer_ui_target.generation,
                                    }};

    RL_APP_INFO("bootstrap complete");

    rl::platform::input_state input_state;
    rl::platform::event_dispatcher event_dispatcher;
    const rl::platform::input_action_map input_actions = make_input_action_map();

    bool running = true;
    renderer.set_suspended(window.state().minimized);
    register_event_listeners(event_dispatcher, renderer);

    window.show();
    RL_APP_INFO("entering main loop");

    while (running && !window.state().close_requested) {
      const std::vector<rl::platform::platform_event> events =
          window.poll_events([&imgui_layer](const SDL_Event& event) { imgui_layer.handle_event(event); });
      process_platform_events(events, input_state, event_dispatcher, input_actions, running);
      renderer.apply_pending_settings();

      const rl::vulkan::renderer_ui_render_target current_renderer_ui_target = renderer.ui_render_target();
      imgui_layer.update_render_target(rl::ui::imgui_render_target{
        .color_format = current_renderer_ui_target.color_format,
        .min_image_count = current_renderer_ui_target.min_image_count,
        .image_count = current_renderer_ui_target.image_count,
        .generation = current_renderer_ui_target.generation,
      });

      imgui_layer.begin_frame();
      imgui_layer.draw_renderer_panel(renderer.status(), renderer);
      imgui_layer.end_frame();

      if (renderer.suspended()) {
        sleep_until_next_frame(renderer.suspended());
      } else {
        renderer.draw_frame([&imgui_layer](VkCommandBuffer command_buffer) { imgui_layer.render(command_buffer); });
      }
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
