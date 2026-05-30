#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "base/log_category.hpp"
#include "base/log_level.hpp"
#include "base/log_ring_buffer.hpp"

namespace rl::log {
struct logger_config {
  std::string application_name = "renderlab";

  bool enable_console_sink = true;
  bool enable_file_sink = true;
  bool enable_memory_sink = true;

  std::filesystem::path log_directory = "logs";
  bool truncate_file = true;

#ifndef NDEBUG
  log_level level = log_level::trace;
#else
  log_level level = log_level::info;
#endif

  log_level flush_level = log_level::warn;

  std::size_t ring_buffer_capacity = 4096;

  bool async = false;
  bool async_overrun_oldest = false;
  std::size_t async_queue_size = 8192;
  std::size_t async_thread_count = 1;

  std::chrono::seconds periodic_flush_interval{3};

  std::string console_pattern = "%^[%H:%M:%S.%e] [%n] [%l]%$ %v";
  std::string file_pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [thread %t] [%s:%#] %v";
};

void initialize(logger_config config = {});
void shutdown() noexcept;

[[nodiscard]] bool is_initialized();
[[nodiscard]] std::shared_ptr<spdlog::logger> logger(log_category log_category);
[[nodiscard]] std::shared_ptr<log_ring_buffer> ring_buffer();
[[nodiscard]] std::filesystem::path current_log_file();

void set_level(log_level level);
void set_level(log_category log_category, log_level level);
void flush();

class scoped_logger final {
 public:
  explicit scoped_logger(logger_config config = {}) { initialize(std::move(config)); }

  ~scoped_logger() noexcept { shutdown(); }

  scoped_logger(const scoped_logger&) = delete;
  scoped_logger& operator=(const scoped_logger&) = delete;
  scoped_logger(scoped_logger&&) = delete;
  scoped_logger& operator=(scoped_logger&&) = delete;
};

template <typename... Args>
void write(log_category log_category, log_level level, spdlog::source_loc source_location,
           fmt::format_string<Args...> format, Args&&... args) {
  const auto target_logger = logger(log_category);
  if (target_logger == nullptr) {
    return;
  }

  target_logger->log(source_location, to_spdlog(level), format, std::forward<Args>(args)...);
}
}  // namespace rl::log

#define RL_LOG_TRACE(log_category, ...)                         \
  ::rl::log::write((log_category), ::rl::log::log_level::trace, \
                   spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, __VA_ARGS__)

#define RL_LOG_DEBUG(log_category, ...)                         \
  ::rl::log::write((log_category), ::rl::log::log_level::debug, \
                   spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, __VA_ARGS__)

#define RL_LOG_INFO(log_category, ...)                         \
  ::rl::log::write((log_category), ::rl::log::log_level::info, \
                   spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, __VA_ARGS__)

#define RL_LOG_WARN(log_category, ...)                         \
  ::rl::log::write((log_category), ::rl::log::log_level::warn, \
                   spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, __VA_ARGS__)

#define RL_LOG_ERROR(log_category, ...)                         \
  ::rl::log::write((log_category), ::rl::log::log_level::error, \
                   spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, __VA_ARGS__)

#define RL_LOG_CRITICAL(log_category, ...)                         \
  ::rl::log::write((log_category), ::rl::log::log_level::critical, \
                   spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, __VA_ARGS__)

#define RL_CORE_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::core, __VA_ARGS__)
#define RL_CORE_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::core, __VA_ARGS__)
#define RL_CORE_INFO(...) RL_LOG_INFO(::rl::log::log_category::core, __VA_ARGS__)
#define RL_CORE_WARN(...) RL_LOG_WARN(::rl::log::log_category::core, __VA_ARGS__)
#define RL_CORE_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::core, __VA_ARGS__)
#define RL_CORE_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::core, __VA_ARGS__)

#define RL_APP_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::app, __VA_ARGS__)
#define RL_APP_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::app, __VA_ARGS__)
#define RL_APP_INFO(...) RL_LOG_INFO(::rl::log::log_category::app, __VA_ARGS__)
#define RL_APP_WARN(...) RL_LOG_WARN(::rl::log::log_category::app, __VA_ARGS__)
#define RL_APP_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::app, __VA_ARGS__)
#define RL_APP_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::app, __VA_ARGS__)

#define RL_PLATFORM_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::platform, __VA_ARGS__)
#define RL_PLATFORM_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::platform, __VA_ARGS__)
#define RL_PLATFORM_INFO(...) RL_LOG_INFO(::rl::log::log_category::platform, __VA_ARGS__)
#define RL_PLATFORM_WARN(...) RL_LOG_WARN(::rl::log::log_category::platform, __VA_ARGS__)
#define RL_PLATFORM_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::platform, __VA_ARGS__)
#define RL_PLATFORM_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::platform, __VA_ARGS__)

#define RL_VK_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::vulkan, __VA_ARGS__)
#define RL_VK_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::vulkan, __VA_ARGS__)
#define RL_VK_INFO(...) RL_LOG_INFO(::rl::log::log_category::vulkan, __VA_ARGS__)
#define RL_VK_WARN(...) RL_LOG_WARN(::rl::log::log_category::vulkan, __VA_ARGS__)
#define RL_VK_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::vulkan, __VA_ARGS__)
#define RL_VK_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::vulkan, __VA_ARGS__)

#define RL_GPU_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::gpu, __VA_ARGS__)
#define RL_GPU_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::gpu, __VA_ARGS__)
#define RL_GPU_INFO(...) RL_LOG_INFO(::rl::log::log_category::gpu, __VA_ARGS__)
#define RL_GPU_WARN(...) RL_LOG_WARN(::rl::log::log_category::gpu, __VA_ARGS__)
#define RL_GPU_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::gpu, __VA_ARGS__)
#define RL_GPU_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::gpu, __VA_ARGS__)

#define RL_RENDER_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::render, __VA_ARGS__)
#define RL_RENDER_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::render, __VA_ARGS__)
#define RL_RENDER_INFO(...) RL_LOG_INFO(::rl::log::log_category::render, __VA_ARGS__)
#define RL_RENDER_WARN(...) RL_LOG_WARN(::rl::log::log_category::render, __VA_ARGS__)
#define RL_RENDER_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::render, __VA_ARGS__)
#define RL_RENDER_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::render, __VA_ARGS__)

#define RL_SHADER_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::shader, __VA_ARGS__)
#define RL_SHADER_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::shader, __VA_ARGS__)
#define RL_SHADER_INFO(...) RL_LOG_INFO(::rl::log::log_category::shader, __VA_ARGS__)
#define RL_SHADER_WARN(...) RL_LOG_WARN(::rl::log::log_category::shader, __VA_ARGS__)
#define RL_SHADER_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::shader, __VA_ARGS__)
#define RL_SHADER_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::shader, __VA_ARGS__)

#define RL_ASSET_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::assets, __VA_ARGS__)
#define RL_ASSET_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::assets, __VA_ARGS__)
#define RL_ASSET_INFO(...) RL_LOG_INFO(::rl::log::log_category::assets, __VA_ARGS__)
#define RL_ASSET_WARN(...) RL_LOG_WARN(::rl::log::log_category::assets, __VA_ARGS__)
#define RL_ASSET_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::assets, __VA_ARGS__)
#define RL_ASSET_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::assets, __VA_ARGS__)

#define RL_UI_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::ui, __VA_ARGS__)
#define RL_UI_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::ui, __VA_ARGS__)
#define RL_UI_INFO(...) RL_LOG_INFO(::rl::log::log_category::ui, __VA_ARGS__)
#define RL_UI_WARN(...) RL_LOG_WARN(::rl::log::log_category::ui, __VA_ARGS__)
#define RL_UI_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::ui, __VA_ARGS__)
#define RL_UI_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::ui, __VA_ARGS__)

#define RL_PROFILE_TRACE(...) RL_LOG_TRACE(::rl::log::log_category::profiling, __VA_ARGS__)
#define RL_PROFILE_DEBUG(...) RL_LOG_DEBUG(::rl::log::log_category::profiling, __VA_ARGS__)
#define RL_PROFILE_INFO(...) RL_LOG_INFO(::rl::log::log_category::profiling, __VA_ARGS__)
#define RL_PROFILE_WARN(...) RL_LOG_WARN(::rl::log::log_category::profiling, __VA_ARGS__)
#define RL_PROFILE_ERROR(...) RL_LOG_ERROR(::rl::log::log_category::profiling, __VA_ARGS__)
#define RL_PROFILE_CRITICAL(...) RL_LOG_CRITICAL(::rl::log::log_category::profiling, __VA_ARGS__)
