#pragma once

#include <cstdint>

#include <spdlog/common.h>

namespace rl::log {

enum class log_level : std::uint8_t {
  trace,
  debug,
  info,
  warn,
  error,
  critical,
  off,
};

[[nodiscard]] constexpr spdlog::level::level_enum to_spdlog(log_level level) noexcept {
  switch (level) {
    case log_level::trace:
      return spdlog::level::trace;
    case log_level::debug:
      return spdlog::level::debug;
    case log_level::info:
      return spdlog::level::info;
    case log_level::warn:
      return spdlog::level::warn;
    case log_level::error:
      return spdlog::level::err;
    case log_level::critical:
      return spdlog::level::critical;
    case log_level::off:
      return spdlog::level::off;
  }

  return spdlog::level::info;
}

[[nodiscard]] constexpr log_level from_spdlog(spdlog::level::level_enum level) noexcept {
  switch (level) {
    case spdlog::level::trace:
      return log_level::trace;
    case spdlog::level::debug:
      return log_level::debug;
    case spdlog::level::info:
      return log_level::info;
    case spdlog::level::warn:
      return log_level::warn;
    case spdlog::level::err:
      return log_level::error;
    case spdlog::level::critical:
      return log_level::critical;
    case spdlog::level::off:
    case spdlog::level::n_levels:
      return log_level::off;
  }

  return log_level::info;
}

[[nodiscard]] constexpr std::string_view to_string(log_level level) noexcept {
  switch (level) {
    case log_level::trace:
      return "trace";
    case log_level::debug:
      return "debug";
    case log_level::info:
      return "info";
    case log_level::warn:
      return "warn";
    case log_level::error:
      return "error";
    case log_level::critical:
      return "critical";
    case log_level::off:
      return "off";
  }

  return "unknown";
}

}  // namespace rl::log
