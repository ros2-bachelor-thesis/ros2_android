#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <chrono>
#include <unordered_map>

namespace ros2_android
{

  class CameraFrameCallbackQueue
  {
  public:
    using CallbackType = std::function<void(const std::string &camera_id)>;

    static CameraFrameCallbackQueue &Instance()
    {
      static CameraFrameCallbackQueue instance;
      return instance;
    }

    void SetCallback(CallbackType callback)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback_ = std::move(callback);
    }

    // Throttled post - only fires callback if > min_interval since last
    void Post(const std::string &camera_id,
              std::chrono::milliseconds min_interval = std::chrono::milliseconds(10))
    {
      auto now = std::chrono::steady_clock::now();

      CallbackType callback_copy;
      {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check throttle
        auto &last_time = last_post_time_[camera_id];
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
        callback_copy(camera_id);
      }
    }

  private:
    CameraFrameCallbackQueue() = default;
    CameraFrameCallbackQueue(const CameraFrameCallbackQueue &) = delete;
    CameraFrameCallbackQueue &operator=(const CameraFrameCallbackQueue &) = delete;

    std::mutex mutex_;
    CallbackType callback_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_post_time_;
  };

  inline void PostCameraFrameUpdate(const std::string &camera_id)
  {
    CameraFrameCallbackQueue::Instance().Post(camera_id);
  }

} // namespace ros2_android
