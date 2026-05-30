#include "base/log_ring_buffer.hpp"

#include <cstddef>
#include <mutex>
#include <utility>

namespace rl::log {

log_ring_buffer::log_ring_buffer(std::size_t capacity) : capacity_{capacity == 0 ? 1 : capacity} {
  entries_.reserve(capacity_);
}

void log_ring_buffer::push(log_entry entry) {
  const std::scoped_lock lock{mutex_};

  if (entries_.size() < capacity_) {
    entries_.push_back(std::move(entry));
    return;
  }

  entries_.at(next_) = std::move(entry);
  next_ = (next_ + 1) % capacity_;
  wrapped_ = true;
}

void log_ring_buffer::clear() {
  const std::scoped_lock lock{mutex_};

  entries_.clear();
  next_ = 0;
  wrapped_ = false;
}

std::vector<log_entry> log_ring_buffer::snapshot() const {
  const std::scoped_lock lock{mutex_};

  if (!wrapped_) {
    return entries_;
  }

  std::vector<log_entry> result;
  result.reserve(entries_.size());

  for (std::size_t offset = 0; offset < entries_.size(); ++offset) {
    const std::size_t index = (next_ + offset) % entries_.size();
    result.push_back(entries_.at(index));
  }

  return result;
}

std::size_t log_ring_buffer::size() const {
  const std::scoped_lock lock{mutex_};
  return entries_.size();
}

std::size_t log_ring_buffer::capacity() const noexcept { return capacity_; }

}  // namespace rl::log
