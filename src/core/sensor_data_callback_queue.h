#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <chrono>
#include <unordered_map>

namespace ros2_android
{

  class SensorDataCallbackQueue
  {
  public:
    using CallbackType = std::function<void(const std::string &sensor_id)>;

    static SensorDataCallbackQueue &Instance()
    {
      static SensorDataCallbackQueue instance;
      return instance;
    }

    void SetCallback(CallbackType callback)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback_ = std::move(callback);
    }

    // Throttled post - only fires callback if > min_interval since last
    void Post(const std::string &sensor_id,
              std::chrono::milliseconds min_interval = std::chrono::milliseconds(100))
    {
      auto now = std::chrono::steady_clock::now();

      CallbackType callback_copy;
      {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check throttle
        auto &last_time = last_post_time_[sensor_id];
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);
        if (elapsed < min_interval)
        {
          return; // Skip this update
        }
        last_time = now;

        callback_copy = callback_;
      }

      // Call outside lock to avoid deadlock
      if (callback_copy)
      {
        callback_copy(sensor_id);
      }
    }

  private:
    SensorDataCallbackQueue() = default;
    SensorDataCallbackQueue(const SensorDataCallbackQueue &) = delete;
    SensorDataCallbackQueue &operator=(const SensorDataCallbackQueue &) = delete;

    std::mutex mutex_;
    CallbackType callback_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_post_time_;
  };

  inline void PostSensorDataUpdate(const std::string &sensor_id)
  {
    SensorDataCallbackQueue::Instance().Post(sensor_id);
  }

} // namespace ros2_android
