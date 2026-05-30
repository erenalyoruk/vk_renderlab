#include "base/log.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "base/log_category.hpp"

namespace rl::log {

namespace {

class ring_buffer_sink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  explicit ring_buffer_sink(std::shared_ptr<log_ring_buffer> buffer) : buffer_{std::move(buffer)} {}

 protected:
  void sink_it_(const spdlog::details::log_msg& message) override {
    if (buffer_ == nullptr) {
      return;
    }

    log_entry entry{};
    entry.timestamp = message.time;
    entry.level = from_spdlog(message.level);
    entry.logger_name = copy_string_view(message.logger_name);
    entry.thread_id = message.thread_id;
    entry.source_file = message.source.filename != nullptr ? message.source.filename : "";
    entry.source_line = message.source.line;
    entry.source_function = message.source.funcname != nullptr ? message.source.funcname : "";
    entry.message = copy_string_view(message.payload);

    buffer_->push(std::move(entry));
  }

  void flush_() override {}

 private:
  [[nodiscard]] static std::string copy_string_view(spdlog::string_view_t view) {
    if (view.size() == 0) {
      return {};
    }

    return {view.data(), view.size()};
  }

  std::shared_ptr<log_ring_buffer> buffer_;
};

struct registry {
  std::mutex mutex;

  bool initialized = false;
  logger_config config{};

  std::vector<spdlog::sink_ptr> sinks;
  std::array<std::shared_ptr<spdlog::logger>, category_count> loggers{};

  std::shared_ptr<log_ring_buffer> ring_buffer;
  std::filesystem::path log_file;
};

[[nodiscard]] registry& registry() {
  static struct registry instance;
  return instance;
}

[[nodiscard]] std::filesystem::path make_log_file_path(const logger_config& config) {
  const std::string application_name =
      config.application_name.empty() ? std::string{"renderlab"} : config.application_name;

  if (config.log_directory.empty()) {
    return application_name + ".log";
  }

  std::filesystem::create_directories(config.log_directory);
  return config.log_directory / (application_name + ".log");
}

[[nodiscard]] std::shared_ptr<spdlog::logger> logger_locked(struct registry& registry, log_category category) {
  const std::size_t index = category_index(category);
  if (index >= registry.loggers.size()) {
    return registry.loggers[category_index(log_category::core)];
  }

  return registry.loggers.at(index);
}

void shutdown_locked(struct registry& registry) noexcept {
  try {
    for (const auto& logger : registry.loggers) {
      if (logger != nullptr) {
        logger->flush();
      }
    }

    spdlog::shutdown();

    registry.loggers.fill(nullptr);
    registry.sinks.clear();
    registry.ring_buffer.reset();
    registry.log_file.clear();
    registry.initialized = false;
  } catch (...) {
    registry.loggers.fill(nullptr);
    registry.sinks.clear();
    registry.ring_buffer.reset();
    registry.log_file.clear();
    registry.initialized = false;
  }
}

[[nodiscard]] spdlog::async_overflow_policy async_overflow_policy(const logger_config& config) {
  if (config.async_overrun_oldest) {
    return spdlog::async_overflow_policy::overrun_oldest;
  }

  return spdlog::async_overflow_policy::block;
}

[[nodiscard]] std::shared_ptr<spdlog::logger> make_logger(const std::string& name,
                                                          const std::vector<spdlog::sink_ptr>& sinks,
                                                          const logger_config& config) {
  if (config.async) {
    return std::make_shared<spdlog::async_logger>(name, sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                  async_overflow_policy(config));
  }

  return std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
}

}  // namespace

void initialize(logger_config config) {
  auto& state = registry();
  std::scoped_lock lock{state.mutex};

  shutdown_locked(state);

  spdlog::set_error_handler(
      [](const std::string& message) { std::println(stderr, "[renderlab][logging-error] {}", message); });

  state.config = std::move(config);

  if (state.config.async) {
    const std::size_t queue_size = state.config.async_queue_size == 0 ? 8192 : state.config.async_queue_size;
    const std::size_t thread_count = state.config.async_thread_count == 0 ? 1 : state.config.async_thread_count;

    spdlog::init_thread_pool(queue_size, thread_count);
  }

  if (state.config.enable_console_sink) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern(state.config.console_pattern);
    state.sinks.push_back(std::move(console_sink));
  }

  if (state.config.enable_file_sink) {
    state.log_file = make_log_file_path(state.config);

    auto file_sink =
        std::make_shared<spdlog::sinks::basic_file_sink_mt>(state.log_file.string(), state.config.truncate_file);
    file_sink->set_pattern(state.config.file_pattern);
    state.sinks.push_back(std::move(file_sink));
  }

  if (state.config.enable_memory_sink) {
    state.ring_buffer = std::make_shared<log_ring_buffer>(state.config.ring_buffer_capacity);
    state.sinks.push_back(std::make_shared<ring_buffer_sink>(state.ring_buffer));
  }

  if (state.sinks.empty()) {
    state.sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
  }

  for (std::size_t index = 0; index < category_count; ++index) {
    const auto category = static_cast<log_category>(index);
    auto category_logger = make_logger(std::string{to_string(category)}, state.sinks, state.config);

    category_logger->set_level(to_spdlog(state.config.level));
    category_logger->flush_on(to_spdlog(state.config.flush_level));

    state.loggers.at(index) = std::move(category_logger);
  }

  const std::size_t core_index = category_index(log_category::core);
  spdlog::set_default_logger(state.loggers[core_index]);

  for (std::size_t index = 0; index < category_count; ++index) {
    if (index == core_index) {
      continue;
    }

    spdlog::register_logger(state.loggers.at(index));
  }

  if (state.config.periodic_flush_interval.count() > 0) {
    spdlog::flush_every(state.config.periodic_flush_interval);
  }

  state.initialized = true;

  const std::string log_file = state.log_file.empty() ? std::string{"<disabled>"} : state.log_file.string();

  state.loggers[core_index]->info("logging initialized: level={}, file='{}', async={}, memory_sink={}",
                                  to_string(state.config.level), log_file, state.config.async,
                                  state.config.enable_memory_sink);
}

void shutdown() noexcept {
  auto& state = registry();
  std::scoped_lock lock{state.mutex};
  shutdown_locked(state);
}

bool is_initialized() {
  auto& state = registry();
  std::scoped_lock lock{state.mutex};
  return state.initialized;
}

std::shared_ptr<spdlog::logger> logger(log_category category) {
  auto& state = registry();

  {
    std::scoped_lock lock{state.mutex};
    if (state.initialized) {
      return logger_locked(state, category);
    }
  }

  initialize(logger_config{});

  std::scoped_lock lock{state.mutex};
  return logger_locked(state, category);
}

std::shared_ptr<log_ring_buffer> ring_buffer() {
  auto& state = registry();
  std::scoped_lock lock{state.mutex};
  return state.ring_buffer;
}

std::filesystem::path current_log_file() {
  auto& state = registry();
  std::scoped_lock lock{state.mutex};
  return state.log_file;
}

void set_level(log_level level) {
  auto& state = registry();

  if (!is_initialized()) {
    initialize(logger_config{});
  }

  std::scoped_lock lock{state.mutex};

  for (const auto& logger : state.loggers) {
    if (logger != nullptr) {
      logger->set_level(to_spdlog(level));
    }
  }

  state.config.level = level;
}

void set_level(log_category category, log_level level) {
  const auto target_logger = logger(category);
  if (target_logger != nullptr) {
    target_logger->set_level(to_spdlog(level));
  }
}

void flush() {
  auto& state = registry();

  std::array<std::shared_ptr<spdlog::logger>, category_count> loggers{};

  {
    std::scoped_lock lock{state.mutex};
    loggers = state.loggers;
  }

  for (const auto& logger : loggers) {
    if (logger != nullptr) {
      logger->flush();
    }
  }
}

}  // namespace rl::log
