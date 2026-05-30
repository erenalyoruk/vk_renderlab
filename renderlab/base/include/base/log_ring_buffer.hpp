#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "base/log_level.hpp"

namespace rl::log {

struct log_entry {
  std::chrono::system_clock::time_point timestamp;
  log_level level = log_level::info;

  std::string logger_name;
  std::size_t thread_id = 0;

  std::string source_file;
  std::uint32_t source_line = 0;
  std::string source_function;

  std::string message;
};

class log_ring_buffer final {
 public:
  explicit log_ring_buffer(std::size_t capacity);

  void push(log_entry entry);
  void clear();

  [[nodiscard]] std::vector<log_entry> snapshot() const;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::size_t capacity() const noexcept;

 private:
  mutable std::mutex mutex_;

  std::vector<log_entry> entries_;
  std::size_t capacity_ = 0;
  std::size_t next_ = 0;
  bool wrapped_ = false;
};

}  // namespace rl::log
