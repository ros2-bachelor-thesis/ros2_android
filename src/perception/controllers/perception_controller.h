#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <geometry_msgs/msg/point.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include "perception/object_detection_controller.h"
#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"

namespace ros2_android {

/**
 * Simple 3D point structure (replaces cv::Point3f)
 */
struct Point3f {
  float x;
  float y;
  float z;

  Point3f(float x_ = 0.0f, float y_ = 0.0f, float z_ = 0.0f)
      : x(x_), y(y_), z(z_) {}
};

/**
 * Simple rectangle structure (replaces cv::Rect)
 */
struct Rect {
  int x;
  int y;
  int width;
  int height;

  Rect(int x_ = 0, int y_ = 0, int w_ = 0, int h_ = 0)
      : x(x_), y(y_), width(w_), height(h_) {}
};

/**
 * PerceptionController - YOLOv9 + Deep SORT object detection and tracking
 *
 * Architecture:
 * - Subscribes to 3 external ZED camera topics (RGB, depth, point cloud)
 * - Message synchronization with ~50ms time window
 * - Dedicated inference thread for NCNN processing
 * - 3D localization using point cloud lookup
 * - Publishes 6 topics (3 classes × 2 message types: Point + PointCloud2)
 * - CSV logging for validation against Python reference
 *
 * Classes detected:
 * - 0: cpb_beetle (Colorado Potato Beetle adult)
 * - 1: cpb_larva (larvae)
 * - 2: cpb_eggs (egg clusters)
 */
class PerceptionController : public SensorDataProvider {
 public:
  /**
   * Constructor
   * @param ros ROS interface singleton
   * @param models_path Path to NCNN model files (from Android internal storage)
   */
  PerceptionController(RosInterface& ros, const std::string& models_path);

  ~PerceptionController() override;

  // SensorDataProvider interface
  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  void Enable() override;
  void Disable() override;
  bool IsEnabled() const override { return enabled_; }

  /**
   * Check if models loaded successfully
   */
  bool IsReady() const { return detector_ && detector_->IsReady(); }

  /**
   * Get detection statistics
   */
  size_t GetTotalDetections() const { return total_detections_; }
  size_t GetActiveTrackCount() const { return active_tracks_; }

 private:
  // ============================================================================
  // Latest message storage (matches Python reference approach)
  // ============================================================================

  /**
   * Latest messages from ZED topics (no timestamp synchronization)
   */
  sensor_msgs::msg::CompressedImage::SharedPtr latest_rgb_;
  sensor_msgs::msg::Image::SharedPtr latest_depth_;
  sensor_msgs::msg::PointCloud2::SharedPtr latest_cloud_;
  std::mutex latest_mutex_;

  // ============================================================================
  // ROS infrastructure
  // ============================================================================

  RosInterface& ros_;
  bool enabled_ = false;

  // Subscriptions (from external ZED camera device)
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr rgb_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr depth_sub_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_sub_;

  // Publishers (6 topics: 3 classes × 2 types)
  Publisher<geometry_msgs::msg::Point> pub_beetle_center_;
  Publisher<sensor_msgs::msg::PointCloud2> pub_beetle_;
  Publisher<geometry_msgs::msg::Point> pub_larva_center_;
  Publisher<sensor_msgs::msg::PointCloud2> pub_larva_;
  Publisher<geometry_msgs::msg::Point> pub_eggs_center_;
  Publisher<sensor_msgs::msg::PointCloud2> pub_eggs_;

  // ============================================================================
  // ML pipeline (NCNN YOLOv9 + Deep SORT)
  // ============================================================================

  std::string models_path_;
  std::unique_ptr<perception::ObjectDetectionController> detector_;

  // ============================================================================
  // Inference thread and processing
  // ============================================================================

  std::thread inference_thread_;
  std::atomic<bool> running_{false};

  // Statistics
  std::atomic<size_t> total_detections_{0};
  std::atomic<size_t> active_tracks_{0};

  // ============================================================================
  // CSV logging for validation
  // ============================================================================

  std::ofstream csv_file_;
  std::mutex csv_mutex_;

  // ============================================================================
  // Callback handlers
  // ============================================================================

  /**
   * RGB image callback (JPEG compressed)
   */
  void OnRGB(const sensor_msgs::msg::CompressedImage::SharedPtr msg);

  /**
   * Depth image callback (32FC1 depth map)
   */
  void OnDepth(const sensor_msgs::msg::Image::SharedPtr msg);

  /**
   * Point cloud callback (PointCloud2)
   */
  void OnPointCloud(const sensor_msgs::msg::PointCloud2::SharedPtr msg);

  // ============================================================================
  // Inference thread
  // ============================================================================

  /**
   * Process one frame (RGB + depth + point cloud)
   * Called from OnRGB when all 3 messages are available
   */
  void ProcessFrame(
      const sensor_msgs::msg::CompressedImage::SharedPtr& rgb,
      const sensor_msgs::msg::Image::SharedPtr& depth,
      const sensor_msgs::msg::PointCloud2::SharedPtr& cloud);

  // ============================================================================
  // 3D localization
  // ============================================================================

  /**
   * Get 3D world coordinates from point cloud at bbox center
   * @param bbox Bounding box in image coordinates
   * @param cloud Point cloud message
   * @return 3D point (x, y, z) or (NaN, NaN, NaN) if invalid
   */
  Point3f Get3DLocation(const Rect& bbox,
                        const sensor_msgs::msg::PointCloud2& cloud);

  /**
   * Crop point cloud to bbox region
   * @param bbox Bounding box in image coordinates
   * @param cloud Point cloud message
   * @return Cropped point cloud (or nullptr if invalid)
   */
  sensor_msgs::msg::PointCloud2::UniquePtr CropPointCloud(
      const Rect& bbox,
      const sensor_msgs::msg::PointCloud2& cloud);

  // ============================================================================
  // Publishing and logging
  // ============================================================================

  /**
   * Publish detection results for one track
   * @param track Detection track from Deep SORT
   * @param point3d 3D world coordinates
   * @param cropped_cloud Cropped point cloud for this detection
   * @param header Message header (for timestamp)
   */
  void PublishDetection(const perception::Track& track,
                        const Point3f& point3d,
                        sensor_msgs::msg::PointCloud2::UniquePtr cropped_cloud,
                        const std_msgs::msg::Header& header);

  /**
   * Log detection to CSV file for validation
   */
  void LogDetection(const builtin_interfaces::msg::Time& stamp,
                    const perception::Track& track,
                    const Point3f& point3d);

  /**
   * Initialize CSV log file
   */
  void InitializeCSV();

  /**
   * Close CSV log file
   */
  void CloseCSV();
};

}  // namespace ros2_android
