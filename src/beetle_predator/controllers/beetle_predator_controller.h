#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

  // Capture one still frame, run detection, publish results.
  // Blocks until detection is complete (~300-900 ms). Call from a background thread.
  void TakeSnapshot();

  // Returns JSON of last snapshot result: {latitude, longitude, altitude, accuracy,
  // has_gps, detections:[{label,class_id,confidence,bbox_x,bbox_y,bbox_w,bbox_h}]}
  // Returns "{}" if no snapshot has been taken yet.
  std::string GetLastDetectionsJson() const;

 private:
  RosInterface& ros_;
  CameraController* rear_camera_;
  GpsLocationProvider* gps_provider_;
  bool enabled_ = false;

  // ML pipeline (snapshot mode: YOLO only, no Deep SORT)
  std::string models_path_;
  std::unique_ptr<perception::ObjectDetectionController> detector_;

  // ROS publisher
  Publisher<vermin_collector_ros_msgs::msg::BeetleDetection> detection_pub_;

  // Label filter bitmask (bit 0=beetle, 1=larva, 2=eggs)
  std::atomic<uint8_t> label_mask_{0x07};

  // Detection counter
  std::atomic<int> new_detection_count_{0};

  // Debug frame storage (keyed by frame_id)
  mutable std::mutex debug_frames_mutex_;
  std::map<std::string, std::vector<uint8_t>> debug_frames_jpeg_;
  std::atomic<bool> visualization_enabled_{false};

  // Snapshot processing guard
  std::atomic<bool> processing_{false};

  // Live viewfinder preview thread (10 Hz raw camera frames)
  std::thread preview_thread_;
  std::atomic<bool> preview_running_{false};

  // Last snapshot result
  struct SnapshotEntry {
    std::string label;
    int class_id;
    float confidence;
    int bbox_x, bbox_y, bbox_w, bbox_h;
  };
  struct SnapshotResult {
    double lat = 0.0, lon = 0.0, alt = 0.0;
    float accuracy = -1.0f;
    bool has_gps = false;
    std::vector<SnapshotEntry> detections;
  };
  SnapshotResult last_snapshot_;
  mutable std::mutex snapshot_mutex_;

  void StartPreviewThread();
  void StopPreviewThread();
  void EncodeAndPostFrame(const std::vector<uint8_t>& bgr_data, int width, int height,
                          const std::string& frame_id);
};

}  // namespace ros2_android
