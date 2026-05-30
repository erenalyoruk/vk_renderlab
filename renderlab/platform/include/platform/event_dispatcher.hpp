#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "platform/platform_event.hpp"

namespace rl::platform {

class event_dispatcher final {
 public:
  using listener_id = std::uint64_t;
  using event_callback = std::function<void(const platform_event&)>;

  listener_id subscribe(platform_event_type type, event_callback callback);
  listener_id subscribe_all(event_callback callback);

  [[nodiscard]] bool unsubscribe(listener_id listener_id);

  void dispatch(const platform_event& event) const;
  void dispatch(std::span<const platform_event> events) const;

 private:
  struct listener_entry {
    listener_id id = 0;
    std::optional<platform_event_type> type;
    event_callback callback;
  };

  listener_id next_listener_id_ = 1;
  std::vector<listener_entry> listeners_;
};

}  // namespace rl::platform
