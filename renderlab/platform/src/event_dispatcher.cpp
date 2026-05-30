#include "platform/event_dispatcher.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>

namespace rl::platform {

event_dispatcher::listener_id event_dispatcher::subscribe(platform_event_type type, event_callback callback) {
  const listener_id new_listener_id = next_listener_id_++;
  listeners_.push_back(listener_entry{
    .id = new_listener_id,
    .type = type,
    .callback = std::move(callback),
  });

  return new_listener_id;
}

event_dispatcher::listener_id event_dispatcher::subscribe_all(event_callback callback) {
  const listener_id new_listener_id = next_listener_id_++;
  listeners_.push_back(listener_entry{
    .id = new_listener_id,
    .type = std::nullopt,
    .callback = std::move(callback),
  });

  return new_listener_id;
}

bool event_dispatcher::unsubscribe(listener_id listener_id) {
  const auto previous_size = listeners_.size();
  std::erase_if(listeners_, [listener_id](const listener_entry& listener) { return listener.id == listener_id; });

  return listeners_.size() != previous_size;
}

void event_dispatcher::dispatch(const platform_event& event) const {
  for (const listener_entry& listener : listeners_) {
    if (!listener.callback) {
      continue;
    }

    if (listener.type.has_value() && listener.type != event.type) {
      continue;
    }

    listener.callback(event);
  }
}

void event_dispatcher::dispatch(std::span<const platform_event> events) const {
  for (const platform_event& event : events) {
    dispatch(event);
  }
}

}  // namespace rl::platform
