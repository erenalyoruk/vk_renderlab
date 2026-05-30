#pragma once

namespace rl {
class noncopyable {
 public:
  noncopyable(const noncopyable&) = delete;
  noncopyable& operator=(const noncopyable&) = delete;

  noncopyable(noncopyable&&) = delete;
  noncopyable& operator=(noncopyable&&) = delete;

 protected:
  constexpr noncopyable() noexcept = default;
  ~noncopyable() noexcept = default;
};
}  // namespace rl
