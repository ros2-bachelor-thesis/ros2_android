#pragma once

#include <atomic>
#include <condition_variable>
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
   * Enable/disable debug visualization (JPEG encoding + storage)
   * @param enable true to enable, false to disable
   */
  void EnableVisualization(bool enable) { visualization_enabled_ = enable; }

  /**
   * Check if visualization is enabled
   */
  bool IsVisualizationEnabled() const { return visualization_enabled_; }

  /**
   * Get debug frame (JPEG-encoded)
   * @param frame_id "rgb_annotated" or "depth_annotated"
   * @param out_jpeg Output JPEG data
   * @return true if frame available, false otherwise
   */
  bool GetDebugFrame(const std::string& frame_id, std::vector<uint8_t>& out_jpeg);

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

  // Camera readiness flags (matches Python lines 73-75)
  std::atomic<bool> camera_rgb_{false};
  std::atomic<bool> camera_depth_{false};
  std::atomic<bool> camera_pointcloud_{false};

  // Timestamp tracking to prevent infinite message reprocessing
  rclcpp::Time last_processed_rgb_stamp_{0, 0, RCL_ROS_TIME};

  // Debug frame storage for JNI visualization
  std::mutex debug_frames_mutex_;
  std::map<std::string, std::vector<uint8_t>> debug_frames_jpeg_;  // "rgb_annotated", "depth_annotated" → JPEG data
  std::atomic<bool> visualization_enabled_{false};

  // ============================================================================
  // ROS infrastructure
  // ============================================================================

  RosInterface& ros_;
  bool enabled_ = false;

  // 20Hz timer (matches Python line 79: frequency = 20 Hz)
  rclcpp::TimerBase::SharedPtr timer_;
  static constexpr int kFrequencyHz = 20;

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

  /**
   * Timer callback (20Hz) - triggers inference when camera_rgb_ is ready
   */
  void TimerCallback();

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
                        const sensor_msgs::msg::PointCloud2& cloud,
                        int rgb_width, int rgb_height);

  /**
   * Crop point cloud to bbox region with depth filtering
   * @param bbox Bounding box in image coordinates
   * @param cloud Point cloud message
   * @param depth Depth image for outlier filtering (median ±10%)
   * @return Cropped point cloud (or nullptr if invalid)
   */
  sensor_msgs::msg::PointCloud2::UniquePtr CropPointCloud(
      const Rect& bbox,
      const sensor_msgs::msg::PointCloud2& cloud,
      const sensor_msgs::msg::Image& depth,
      int rgb_width, int rgb_height);

  // ============================================================================
  // Publishing and logging
  // ============================================================================

  /**
   * Publish detection results for one detection
   * @param det Detection from YOLO (before Deep SORT)
   * @param point3d 3D world coordinates
   * @param cropped_cloud Cropped point cloud for this detection
   * @param header Message header (for timestamp)
   */
  void PublishDetection(const perception::Detection& det,
                        const Point3f& point3d,
                        sensor_msgs::msg::PointCloud2::UniquePtr cropped_cloud,
                        const std_msgs::msg::Header& header);
};

}  // namespace ros2_android
