#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <geometry_msgs/msg/point.hpp>
#include <point_cloud_interfaces/msg/compressed_point_cloud2.hpp>
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
   * @param frame_id "rgb_annotated"
   * @param out_jpeg Output JPEG data
   * @return true if frame available, false otherwise
   */
  bool GetDebugFrame(const std::string& frame_id, std::vector<uint8_t>& out_jpeg);

 private:
  // ============================================================================
  // Latest message storage (matches Python reference approach)
  // ============================================================================

  /**
   * Latest messages from ZED topics. Pairing strategy: best-effort timestamp
   * validation in TimerCallback rejects depth/cloud samples whose header
   * stamp diverges from the RGB stamp by more than the per-stream tolerance
   * below. RGB is always processed; depth and cloud are nulled out (treated
   * as missing) when stale, mirroring the Python reference behavior.
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

  // Per-stream skew tolerance vs RGB header stamp.
  // Depth: ZED compressed_depth_image_transport adds ~400 ms publisher-side
  // latency; 800 ms gives headroom without pairing grossly stale geometry.
  // Cloud at 1 Hz: natural lag up to 1000 ms; observed publisher-side offset
  // up to ~3 s on loaded Jetson. Use 3500 ms to match worst observed skew.
  static constexpr int kDepthSkewToleranceMs = 800;
  static constexpr int kCloudSkewToleranceMs = 3500;

  // Counters for stale-pair drops, surfaced via JNI in future work.
  std::atomic<uint32_t> depth_stale_drops_{0};
  std::atomic<uint32_t> cloud_stale_drops_{0};

  // Throttle for LOGW emitted when stale pairs are dropped (1 line/sec max).
  rclcpp::Time last_skew_warn_time_{0, 0, RCL_ROS_TIME};

  // Debug frame storage for JNI visualization
  std::mutex debug_frames_mutex_;
  std::map<std::string, std::vector<uint8_t>> debug_frames_jpeg_;  // "rgb_annotated" → JPEG data
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
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr depth_sub_;
  rclcpp::Subscription<point_cloud_interfaces::msg::CompressedPointCloud2>::SharedPtr cloud_sub_;

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

  struct FrameData {
    sensor_msgs::msg::CompressedImage::SharedPtr rgb;
    sensor_msgs::msg::Image::SharedPtr depth;
    sensor_msgs::msg::PointCloud2::SharedPtr cloud;
  };

  std::thread inference_thread_;
  std::atomic<bool> running_{false};

  // Single-slot pending frame: TimerCallback overwrites if inference is busy.
  // Avoids executor-thread blocking while keeping queue bounded.
  std::mutex pending_mutex_;
  std::condition_variable pending_cv_;
  std::optional<FrameData> pending_frame_;

  // ============================================================================
  // Callback handlers
  // ============================================================================

  /**
   * RGB image callback (JPEG compressed)
   */
  void OnRGB(const sensor_msgs::msg::CompressedImage::SharedPtr msg);

  /**
   * Depth image callback (compressedDepth → decoded to 32FC1)
   */
  void OnDepth(const sensor_msgs::msg::CompressedImage::SharedPtr msg);

  /**
   * Point cloud callback (CompressedPointCloud2 zlib → decoded to PointCloud2)
   */
  void OnPointCloud(const point_cloud_interfaces::msg::CompressedPointCloud2::SharedPtr msg);

  /**
   * Timer callback (20Hz) - posts frame to inference_thread_ and returns.
   */
  void TimerCallback();

  // ============================================================================
  // Inference thread
  // ============================================================================

  /**
   * Runs on inference_thread_. Waits for pending_frame_, calls ProcessFrame.
   */
  void InferenceLoop();

  /**
   * Process one frame (RGB + depth + point cloud)
   */
  void ProcessFrame(
      const sensor_msgs::msg::CompressedImage::SharedPtr& rgb,
      const sensor_msgs::msg::Image::SharedPtr& depth,
      const sensor_msgs::msg::PointCloud2::SharedPtr& cloud);

  // ============================================================================
  // 3D localization
  // ============================================================================

  // Python point cloud indexing parameters (object_detection.py yolov9 branch)
  // model_input_size = [640, 352] - YOLO input after resize+crop
  // pointcloud_size = [448, 256] - point cloud dimensions
  static constexpr int kModelInputWidth = 640;
  static constexpr int kModelInputHeight = 352;
  static constexpr int kPointcloudWidth = 448;
  static constexpr int kPointcloudHeight = 256;

  /**
   * Compute flat point cloud index from model_input_size coordinates.
   * Replicates Python formula (object_detection.py yolov9 branch lines 311-316):
   *   x_scaled = floor(x / model_input_size[0] * pointcloud_size[0])
   *   y_scaled = floor(y / model_input_size[1] * pointcloud_size[1])
   *   idx = x_scaled + y_scaled * pointcloud_size[0]
   * cloud_w/cloud_h are the actual cloud dimensions (dynamic, not hardcoded).
   */
  static int GetCloudFlatIndex(int x, int y, int cloud_w, int cloud_h);

  /**
   * Get 3D world coordinates from point cloud at bbox center
   * @param bbox Bounding box in image coordinates
   * @param cloud Point cloud message
   * @return 3D point (x, y, z) or (NaN, NaN, NaN) if invalid
   */
  Point3f Get3DLocation(const Rect& bbox,
                        const sensor_msgs::msg::PointCloud2& cloud);

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
      const sensor_msgs::msg::Image& depth);

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
