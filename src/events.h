#pragma once

#include <functional>

namespace sensors_for_ros {
namespace event {
struct SensorEvent {
  // Sensor handle (ASensorEvent::sensor) uniquely identifying the sensor
  int handle;
};

template <typename EventType>
using Listener = std::function<void(const EventType&)>;

template <typename EventType>
class Emitter {
 public:
  void Emit(const EventType& event) {
    if (event_listener_) {
      event_listener_(event);
    }
  }

  void SetListener(Listener<EventType> listener) { event_listener_ = listener; }

 private:
  Listener<EventType> event_listener_;
};
}  // namespace event
}  // namespace sensors_for_ros
