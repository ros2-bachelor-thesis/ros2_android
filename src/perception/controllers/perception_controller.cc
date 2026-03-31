#include "perception/controllers/perception_controller.h"

#include <sstream>
#include <cmath>

#include "core/log.h"
#include "core/notification_queue.h"

using ros2_android::PerceptionController;
using ros2_android::NotificationSeverity;
using ros2_android::PostNotification;

namespace {

// Class names for logging and output
const char* CLASS_NAMES[] = {"cpb_beetle", "cpb_larva", "cpb_eggs"};

// Time synchronization window (50ms)
constexpr double kSyncWindowSec = 0.05;

// Old message cleanup threshold (500ms)
constexpr double kCleanupThresholdSec = 0.5;

// Inference parameters
constexpr float kConfidenceThreshold = 0.25f;
constexpr float kIouThreshold = 0.45f;

}  // namespace

// ============================================================================
// Constructor and Destructor
// ============================================================================

PerceptionController::PerceptionController(RosInterface& ros,
                                           const std::string& models_path)
    : SensorDataProvider("perception"),
      ros_(ros),
      models_path_(models_path),
      pub_beetle_center_(ros),
      pub_beetle_(ros),
      pub_larva_center_(ros),
      pub_larva_(ros),
      pub_eggs_center_(ros),
      pub_eggs_(ros) {

  LOGI("Initializing PerceptionController with models from: %s", models_path.c_str());

  // Build model file paths
  std::string yolo_param = models_path + "/yolov9_s_pobed.ncnn.param";
  std::string yolo_bin = models_path + "/yolov9_s_pobed.ncnn.bin";
  std::string reid_param = models_path + "/mars-small128.ncnn.param";
  std::string reid_bin = models_path + "/mars-small128.ncnn.bin";

  // Initialize NCNN detector (use_vulkan = false, CPU NEON faster on ARM)
  detector_ = std::make_unique<perception::ObjectDetectionController>(
      yolo_param, yolo_bin, reid_param, reid_bin, false);

  if (!detector_->IsReady()) {
    LOGE("Failed to load perception models from %s", models_path.c_str());
    PostNotification(NotificationSeverity::ERROR,
                     "Failed to load perception models");
    return;
  }

  LOGI("Perception models loaded successfully");

  // Configure publishers with device namespace
  std::string device_prefix = "/" + ros_.GetDeviceId() + "/";

  pub_beetle_center_.SetTopic((device_prefix + "cpb_beetle_center").c_str());
  pub_beetle_.SetTopic((device_prefix + "cpb_beetle").c_str());
  pub_larva_center_.SetTopic((device_prefix + "cpb_larva_center").c_str());
  pub_larva_.SetTopic((device_prefix + "cpb_larva").c_str());
  pub_eggs_center_.SetTopic((device_prefix + "cpb_eggs_center").c_str());
  pub_eggs_.SetTopic((device_prefix + "cpb_eggs").c_str());

  // Use QoS(10) reliable matching Python reference
  auto qos = rclcpp::QoS(10).reliable();
  pub_beetle_center_.SetQos(qos);
  pub_beetle_.SetQos(qos);
  pub_larva_center_.SetQos(qos);
  pub_larva_.SetQos(qos);
  pub_eggs_center_.SetQos(qos);
  pub_eggs_.SetQos(qos);

  LOGI("PerceptionController initialized");
}

PerceptionController::~PerceptionController() {
  Disable();
}

// ============================================================================
// SensorDataProvider Interface
// ============================================================================

std::string PerceptionController::PrettyName() const {
  std::string name = "Object Detection (YOLOv9 + Deep SORT)";
  if (!enabled_) {
    name += " [disabled]";
  } else if (!IsReady()) {
    name += " [model load failed]";
  }
  return name;
}

std::string PerceptionController::GetLastMeasurementJson() {
  std::ostringstream ss;
  ss << "{\"enabled\":" << (enabled_ ? "true" : "false")
     << ",\"total_detections\":" << total_detections_
     << ",\"active_tracks\":" << active_tracks_
     << ",\"queue_size\":" << data_queue_.size()
     << "}";
  return ss.str();
}

bool PerceptionController::GetLastMeasurement(jni::SensorReadingData& out_data) {
  // Perception doesn't provide sensor readings
  return false;
}

// ============================================================================
// Enable/Disable
// ============================================================================

void PerceptionController::Enable() {
  if (enabled_) {
    LOGW("PerceptionController already enabled");
    return;
  }

  if (!IsReady()) {
    LOGE("Cannot enable perception - models not loaded");
    PostNotification(NotificationSeverity::ERROR,
                     "Cannot enable perception - models not loaded");
    return;
  }

  LOGI("Enabling PerceptionController");

  auto node = ros_.get_node();
  if (!node) {
    LOGE("ROS node not initialized");
    return;
  }

  // Subscribe to ZED camera topics (best effort, keep last 1)
  auto qos = rclcpp::QoS(rclcpp::KeepLast(1))
                 .best_effort()
                 .durability_volatile();

  rgb_sub_ = node->create_subscription<sensor_msgs::msg::CompressedImage>(
      "/zed/zed_node/rgb/image_rect_color/compressed", qos,
      std::bind(&PerceptionController::OnRGB, this, std::placeholders::_1));

  depth_sub_ = node->create_subscription<sensor_msgs::msg::Image>(
      "/zed/zed_node/depth/depth_registered", qos,
      std::bind(&PerceptionController::OnDepth, this, std::placeholders::_1));

  cloud_sub_ = node->create_subscription<sensor_msgs::msg::PointCloud2>(
      "/zed/zed_node/point_cloud/cloud_registered", qos,
      std::bind(&PerceptionController::OnPointCloud, this, std::placeholders::_1));

  // Enable publishers
  pub_beetle_center_.Enable();
  pub_beetle_.Enable();
  pub_larva_center_.Enable();
  pub_larva_.Enable();
  pub_eggs_center_.Enable();
  pub_eggs_.Enable();

  // Initialize CSV logging
  InitializeCSV();

  // Start inference thread
  running_ = true;
  inference_thread_ = std::thread(&PerceptionController::InferenceThreadFunc, this);

  enabled_ = true;

  LOGI("PerceptionController enabled");
}

void PerceptionController::Disable() {
  if (!enabled_) {
    return;
  }

  LOGI("Disabling PerceptionController");

  // Stop inference thread
  running_ = false;
  queue_cv_.notify_all();
  if (inference_thread_.joinable()) {
    inference_thread_.join();
  }

  // Clear queues
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::queue<SyncedData> empty;
    std::swap(data_queue_, empty);
  }
  {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    sync_buffer_.clear();
  }

  // Unsubscribe
  rgb_sub_.reset();
  depth_sub_.reset();
  cloud_sub_.reset();

  // Disable publishers
  pub_beetle_center_.Disable();
  pub_beetle_.Disable();
  pub_larva_center_.Disable();
  pub_larva_.Disable();
  pub_eggs_center_.Disable();
  pub_eggs_.Disable();

  // Close CSV
  CloseCSV();

  enabled_ = false;

  LOGI("PerceptionController disabled");
}

// ============================================================================
// Topic Callbacks
// ============================================================================

void PerceptionController::OnRGB(
    const sensor_msgs::msg::CompressedImage::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(sync_mutex_);

  rclcpp::Time t(msg->header.stamp);
  sync_buffer_[t].rgb = msg;
  sync_buffer_[t].timestamp = t;

  TrySynchronize();
  CleanOldMessages();
}

void PerceptionController::OnDepth(
    const sensor_msgs::msg::Image::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(sync_mutex_);

  rclcpp::Time t(msg->header.stamp);
  sync_buffer_[t].depth = msg;
  sync_buffer_[t].timestamp = t;

  TrySynchronize();
  CleanOldMessages();
}

void PerceptionController::OnPointCloud(
    const sensor_msgs::msg::PointCloud2::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(sync_mutex_);

  rclcpp::Time t(msg->header.stamp);
  sync_buffer_[t].cloud = msg;
  sync_buffer_[t].timestamp = t;

  TrySynchronize();
  CleanOldMessages();
}

// ============================================================================
// Message Synchronization
// ============================================================================

void PerceptionController::TrySynchronize() {
  // Note: sync_mutex_ already locked by caller

  auto node = ros_.get_node();
  if (!node) return;

  rclcpp::Time now = node->now();

  // Find complete synced messages within time window
  for (auto it = sync_buffer_.begin(); it != sync_buffer_.end(); ) {
    const auto& [time, data] = *it;

    // Check if message is complete
    if (data.IsComplete()) {
      // Check if within sync window of any message
      bool in_sync = true;
      double max_dt = 0.0;

      if (data.rgb && data.depth) {
        double dt = std::abs((rclcpp::Time(data.rgb->header.stamp) -
                              rclcpp::Time(data.depth->header.stamp)).seconds());
        max_dt = std::max(max_dt, dt);
        if (dt > kSyncWindowSec) in_sync = false;
      }

      if (data.rgb && data.cloud) {
        double dt = std::abs((rclcpp::Time(data.rgb->header.stamp) -
                              rclcpp::Time(data.cloud->header.stamp)).seconds());
        max_dt = std::max(max_dt, dt);
        if (dt > kSyncWindowSec) in_sync = false;
      }

      if (in_sync) {
        // Push to inference queue
        {
          std::lock_guard<std::mutex> qlock(queue_mutex_);
          data_queue_.push(data);
          queue_cv_.notify_one();
        }

        LOGD("Synced messages (dt=%.1fms)", max_dt * 1000.0);

        // Remove from sync buffer
        it = sync_buffer_.erase(it);
        continue;
      }
    }

    ++it;
  }
}

void PerceptionController::CleanOldMessages() {
  // Note: sync_mutex_ already locked by caller

  auto node = ros_.get_node();
  if (!node) return;

  rclcpp::Time now = node->now();

  auto it = sync_buffer_.begin();
  while (it != sync_buffer_.end()) {
    double age = (now - it->first).seconds();
    if (age > kCleanupThresholdSec) {
      LOGD("Cleaning old message (age=%.1fms)", age * 1000.0);
      it = sync_buffer_.erase(it);
    } else {
      ++it;
    }
  }
}

// ============================================================================
// Inference Thread
// ============================================================================

void PerceptionController::InferenceThreadFunc() {
  LOGI("Inference thread started");

  while (running_) {
    SyncedData data;

    // Wait for data
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (data_queue_.empty()) {
        queue_cv_.wait_for(lock, std::chrono::milliseconds(100));
        continue;
      }
      data = data_queue_.front();
      data_queue_.pop();
    }

    // Process frame
    ProcessSyncedData(data);
  }

  LOGI("Inference thread stopped");
}

void PerceptionController::ProcessSyncedData(const SyncedData& data) {
  // Decompress JPEG to cv::Mat
  cv::Mat rgb_image = cv::imdecode(
      cv::Mat(1, data.rgb->data.size(), CV_8UC1, (void*)data.rgb->data.data()),
      cv::IMREAD_COLOR);

  if (rgb_image.empty()) {
    LOGW("Failed to decode JPEG image");
    return;
  }

  // Run NCNN inference (YOLOv9 + Deep SORT)
  auto start = std::chrono::high_resolution_clock::now();
  auto tracks = detector_->ProcessFrame(rgb_image, kConfidenceThreshold, kIouThreshold);
  auto end = std::chrono::high_resolution_clock::now();

  double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

  LOGI("Inference: %zu tracks, %.1f ms (%.1f FPS)",
       tracks.size(), elapsed_ms, 1000.0 / elapsed_ms);

  // Update statistics
  total_detections_ += tracks.size();
  active_tracks_ = tracks.size();

  // Process each detected track
  for (const auto& track : tracks) {
    // Convert bbox to cv::Rect
    cv::Rect bbox(
        static_cast<int>(track.bbox[0]),
        static_cast<int>(track.bbox[1]),
        static_cast<int>(track.bbox[2] - track.bbox[0]),
        static_cast<int>(track.bbox[3] - track.bbox[1]));

    // Get 3D location from point cloud
    cv::Point3f point3d = Get3DLocation(bbox, *data.cloud);

    // Crop point cloud for this detection
    auto cropped_cloud = CropPointCloud(bbox, *data.cloud);

    // Publish results
    PublishDetection(track, point3d, std::move(cropped_cloud), data.rgb->header);

    // Log to CSV
    LogDetection(data.rgb->header.stamp, track, point3d);
  }
}

// ============================================================================
// 3D Localization
// ============================================================================

cv::Point3f PerceptionController::Get3DLocation(
    const cv::Rect& bbox,
    const sensor_msgs::msg::PointCloud2& cloud) {

  // Find center of bbox
  int center_x = bbox.x + bbox.width / 2;
  int center_y = bbox.y + bbox.height / 2;

  // Clamp to image bounds
  center_x = std::max(0, std::min(center_x, static_cast<int>(cloud.width) - 1));
  center_y = std::max(0, std::min(center_y, static_cast<int>(cloud.height) - 1));

  // Find field offsets for x, y, z
  int x_offset = -1, y_offset = -1, z_offset = -1;
  for (const auto& field : cloud.fields) {
    if (field.name == "x") x_offset = field.offset;
    if (field.name == "y") y_offset = field.offset;
    if (field.name == "z") z_offset = field.offset;
  }

  if (x_offset < 0 || y_offset < 0 || z_offset < 0) {
    LOGW("Point cloud missing x/y/z fields");
    return cv::Point3f(NAN, NAN, NAN);
  }

  // Calculate data index
  size_t index = center_y * cloud.row_step + center_x * cloud.point_step;

  if (index + z_offset + sizeof(float) > cloud.data.size()) {
    LOGW("Point cloud index out of bounds");
    return cv::Point3f(NAN, NAN, NAN);
  }

  // Extract XYZ (assumes float32)
  float x = *reinterpret_cast<const float*>(&cloud.data[index + x_offset]);
  float y = *reinterpret_cast<const float*>(&cloud.data[index + y_offset]);
  float z = *reinterpret_cast<const float*>(&cloud.data[index + z_offset]);

  // Check for invalid points
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    LOGD("Invalid point at bbox center (%d, %d)", center_x, center_y);
    return cv::Point3f(NAN, NAN, NAN);
  }

  return cv::Point3f(x, y, z);
}

sensor_msgs::msg::PointCloud2::UniquePtr PerceptionController::CropPointCloud(
    const cv::Rect& bbox,
    const sensor_msgs::msg::PointCloud2& cloud) {

  auto cropped = std::make_unique<sensor_msgs::msg::PointCloud2>();
  cropped->header = cloud.header;
  cropped->fields = cloud.fields;
  cropped->is_bigendian = cloud.is_bigendian;
  cropped->point_step = cloud.point_step;
  cropped->is_dense = false;

  // Clamp bbox to image bounds
  int x1 = std::max(0, bbox.x);
  int y1 = std::max(0, bbox.y);
  int x2 = std::min(static_cast<int>(cloud.width), bbox.x + bbox.width);
  int y2 = std::min(static_cast<int>(cloud.height), bbox.y + bbox.height);

  int crop_width = x2 - x1;
  int crop_height = y2 - y1;

  if (crop_width <= 0 || crop_height <= 0) {
    LOGW("Invalid bbox for cropping");
    return nullptr;
  }

  cropped->width = crop_width;
  cropped->height = crop_height;
  cropped->row_step = crop_width * cloud.point_step;

  // Reserve space
  cropped->data.reserve(crop_height * cropped->row_step);

  // Extract points within bbox
  for (int y = y1; y < y2; ++y) {
    for (int x = x1; x < x2; ++x) {
      size_t src_index = y * cloud.row_step + x * cloud.point_step;

      if (src_index + cloud.point_step <= cloud.data.size()) {
        cropped->data.insert(cropped->data.end(),
                            cloud.data.begin() + src_index,
                            cloud.data.begin() + src_index + cloud.point_step);
      }
    }
  }

  return cropped;
}

// ============================================================================
// Publishing and Logging
// ============================================================================

void PerceptionController::PublishDetection(
    const perception::Track& track,
    const cv::Point3f& point3d,
    sensor_msgs::msg::PointCloud2::UniquePtr cropped_cloud,
    const std_msgs::msg::Header& header) {

  // Publish Point (center location)
  if (std::isfinite(point3d.x) && std::isfinite(point3d.y) && std::isfinite(point3d.z)) {
    geometry_msgs::msg::Point point_msg;
    point_msg.x = point3d.x;
    point_msg.y = point3d.y;
    point_msg.z = point3d.z;

    if (track.class_id == 0) {
      pub_beetle_center_.Publish(point_msg);
    } else if (track.class_id == 1) {
      pub_larva_center_.Publish(point_msg);
    } else if (track.class_id == 2) {
      pub_eggs_center_.Publish(point_msg);
    }
  }

  // Publish PointCloud2 (cropped region)
  if (cropped_cloud && !cropped_cloud->data.empty()) {
    cropped_cloud->header = header;

    if (track.class_id == 0) {
      pub_beetle_.Publish(std::move(cropped_cloud));
    } else if (track.class_id == 1) {
      pub_larva_.Publish(std::move(cropped_cloud));
    } else if (track.class_id == 2) {
      pub_eggs_.Publish(std::move(cropped_cloud));
    }
  }
}

void PerceptionController::LogDetection(
    const builtin_interfaces::msg::Time& stamp,
    const perception::Track& track,
    const cv::Point3f& point3d) {

  std::lock_guard<std::mutex> lock(csv_mutex_);

  if (!csv_file_.is_open()) {
    return;
  }

  csv_file_ << stamp.sec << "." << stamp.nanosec << ","
            << track.track_id << ","
            << static_cast<int>(track.class_id) << ","
            << track.bbox[0] << ","
            << track.bbox[1] << ","
            << track.bbox[2] << ","
            << track.bbox[3] << ","
            << point3d.x << ","
            << point3d.y << ","
            << point3d.z << "\n";
  csv_file_.flush();
}

// ============================================================================
// CSV Management
// ============================================================================

void PerceptionController::InitializeCSV() {
  std::lock_guard<std::mutex> lock(csv_mutex_);

  // Log to Android Download directory (user-accessible)
  std::string path = "/storage/emulated/0/Download/perception_log.csv";

  csv_file_.open(path, std::ios::app);
  if (!csv_file_.is_open()) {
    LOGW("Failed to open CSV log file: %s", path.c_str());
    return;
  }

  // Write header if file is empty (new file)
  csv_file_.seekp(0, std::ios::end);
  if (csv_file_.tellp() == 0) {
    csv_file_ << "timestamp,track_id,class_id,confidence,"
              << "bbox_x1,bbox_y1,bbox_x2,bbox_y2,"
              << "pos_x,pos_y,pos_z\n";
  }

  LOGI("CSV logging enabled: %s", path.c_str());
}

void PerceptionController::CloseCSV() {
  std::lock_guard<std::mutex> lock(csv_mutex_);

  if (csv_file_.is_open()) {
    csv_file_.close();
    LOGI("CSV logging closed");
  }
}
