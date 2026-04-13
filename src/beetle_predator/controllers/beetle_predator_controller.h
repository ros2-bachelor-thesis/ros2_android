#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <vermin_collector_ros_msgs/msg/beetle_detection.hpp>

#include "camera/controllers/camera_controller.h"
#include "perception/object_detection_controller.h"
#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"
#include "sensors/impl/gps_location_sensor.h"

namespace ros2_android {

class BeetlePredatorController : public SensorDataProvider {
 public:
  BeetlePredatorController(RosInterface& ros,
                           const std::string& models_path,
                           CameraController* rear_camera,
                           GpsLocationProvider* gps_provider);

  ~BeetlePredatorController() override;

  // SensorDataProvider interface
  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  void Enable() override;
  void Disable() override;
  bool IsEnabled() const override { return enabled_; }

  bool IsReady() const { return detector_ && detector_->IsReady(); }

  void SetLabelFilter(uint8_t mask) { label_mask_.store(mask); }
  uint8_t GetLabelFilter() const { return label_mask_.load(); }

  void EnableVisualization(bool enable) { visualization_enabled_ = enable; }
  bool IsVisualizationEnabled() const { return visualization_enabled_; }

  bool GetDebugFrame(const std::string& frame_id,
                     std::vector<uint8_t>& out_jpeg);

  int GetNewDetectionCount() const { return new_detection_count_.load(); }

 private:
  RosInterface& ros_;
  CameraController* rear_camera_;
  GpsLocationProvider* gps_provider_;
  bool enabled_ = false;

  // ML pipeline
  std::string models_path_;
  std::unique_ptr<perception::ObjectDetectionController> detector_;

  // ROS publisher
  Publisher<vermin_collector_ros_msgs::msg::BeetleDetection> detection_pub_;

  // Processing timer (1 Hz - matches ~900ms inference time)
  rclcpp::TimerBase::SharedPtr timer_;
  static constexpr int kFrequencyHz = 1;

  // Label filter bitmask (bit 0=beetle, 1=larva, 2=eggs)
  std::atomic<uint8_t> label_mask_{0x07};  // All enabled by default

  // Novelty filter - only publish new tracks
  std::unordered_set<int> published_track_ids_;
  std::mutex novelty_mutex_;

  // Detection counter
  std::atomic<int> new_detection_count_{0};

  // Debug frame storage
  std::mutex debug_frames_mutex_;
  std::map<std::string, std::vector<uint8_t>> debug_frames_jpeg_;
  std::atomic<bool> visualization_enabled_{false};

  // Processing state
  std::atomic<bool> processing_{false};

  void TimerCallback();
  void ProcessFrame();
};

}  // namespace ros2_android
