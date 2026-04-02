#include "perception/controllers/perception_controller.h"

#include <sstream>
#include <cmath>
#include <turbojpeg.h>

#include "core/log.h"
#include "core/notification_queue.h"

using ros2_android::NotificationSeverity;
using ros2_android::PerceptionController;
using ros2_android::Point3f;
using ros2_android::PostNotification;
using ros2_android::Rect;

namespace
{

  // Class names for logging and output
  const char *CLASS_NAMES[] = {"cpb_beetle", "cpb_larva", "cpb_eggs"};

  // Inference parameters
  constexpr float kConfidenceThreshold = 0.25f;
  constexpr float kIouThreshold = 0.45f;

} // namespace

// ============================================================================
// Constructor and Destructor
// ============================================================================

PerceptionController::PerceptionController(RosInterface &ros,
                                           const std::string &models_path)
    : SensorDataProvider("perception"),
      ros_(ros),
      models_path_(models_path),
      pub_beetle_center_(ros),
      pub_beetle_(ros),
      pub_larva_center_(ros),
      pub_larva_(ros),
      pub_eggs_center_(ros),
      pub_eggs_(ros)
{

  LOGI("Initializing PerceptionController with models from: %s", models_path.c_str());

  // Build model file paths
  std::string yolo_param = models_path + "/yolov9_s_pobed.ncnn.param";
  std::string yolo_bin = models_path + "/yolov9_s_pobed.ncnn.bin";
  std::string reid_param = models_path + "/osnet_ain_x1_0.ncnn.param";
  std::string reid_bin = models_path + "/osnet_ain_x1_0.ncnn.bin";

  // Initialize NCNN detector (use_vulkan = false, CPU NEON faster on ARM)
  detector_ = std::make_unique<perception::ObjectDetectionController>(
      yolo_param, yolo_bin, reid_param, reid_bin, false);

  if (!detector_->IsReady())
  {
    LOGE("Failed to load perception models from %s", models_path.c_str());
    PostNotification(NotificationSeverity::ERROR,
                     "Failed to load perception models");
    return;
  }

  LOGI("Perception models loaded successfully");

  // Configure publishers with device namespace
  std::string device_prefix = "/" + ros_.GetDeviceId() + "/";

  pub_beetle_center_.SetTopic("cpb_beetle_center");
  pub_beetle_.SetTopic("cpb_beetle");
  pub_larva_center_.SetTopic("cpb_larva_center");
  pub_larva_.SetTopic("cpb_larva");
  pub_eggs_center_.SetTopic("cpb_eggs_center");
  pub_eggs_.SetTopic("cpb_eggs");

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

PerceptionController::~PerceptionController()
{
  Disable();
}

// ============================================================================
// SensorDataProvider Interface
// ============================================================================

std::string PerceptionController::PrettyName() const
{
  std::string name = "Object Detection (YOLOv9 + Deep SORT)";
  if (!enabled_)
  {
    name += " [disabled]";
  }
  else if (!IsReady())
  {
    name += " [model load failed]";
  }
  return name;
}

std::string PerceptionController::GetLastMeasurementJson()
{
  std::ostringstream ss;
  ss << "{\"enabled\":" << (enabled_ ? "true" : "false")
     << ",\"total_detections\":" << total_detections_
     << ",\"active_tracks\":" << active_tracks_
     << "}";
  return ss.str();
}

bool PerceptionController::GetLastMeasurement(jni::SensorReadingData &out_data)
{
  // Perception doesn't provide sensor readings
  return false;
}

// ============================================================================
// Enable/Disable
// ============================================================================

void PerceptionController::Enable()
{
  if (enabled_)
  {
    LOGW("PerceptionController already enabled");
    return;
  }

  if (!IsReady())
  {
    LOGE("Cannot enable perception - models not loaded");
    PostNotification(NotificationSeverity::ERROR,
                     "Cannot enable perception - models not loaded");
    return;
  }

  LOGI("Enabling PerceptionController");

  auto node = ros_.get_node();
  if (!node)
  {
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
  LOGI("Subscribed to /zed/zed_node/rgb/image_rect_color/compressed");

  depth_sub_ = node->create_subscription<sensor_msgs::msg::Image>(
      "/zed/zed_node/depth/depth_registered", qos,
      std::bind(&PerceptionController::OnDepth, this, std::placeholders::_1));
  LOGI("Subscribed to /zed/zed_node/depth/depth_registered");

  cloud_sub_ = node->create_subscription<sensor_msgs::msg::PointCloud2>(
      "/zed/zed_node/point_cloud/cloud_registered", qos,
      std::bind(&PerceptionController::OnPointCloud, this, std::placeholders::_1));
  LOGI("Subscribed to /zed/zed_node/point_cloud/cloud_registered");

  // Enable publishers
  pub_beetle_center_.Enable();
  pub_beetle_.Enable();
  pub_larva_center_.Enable();
  pub_larva_.Enable();
  pub_eggs_center_.Enable();
  pub_eggs_.Enable();

  enabled_ = true;

  LOGI("PerceptionController enabled - processing on RGB callback");
}

void PerceptionController::Disable()
{
  if (!enabled_)
  {
    return;
  }

  LOGI("Disabling PerceptionController");

  // Clear latest messages
  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    latest_rgb_.reset();
    latest_depth_.reset();
    latest_cloud_.reset();
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

  enabled_ = false;

  LOGI("PerceptionController disabled");
}

// ============================================================================
// Topic Callbacks
// ============================================================================

void PerceptionController::OnRGB(
    const sensor_msgs::msg::CompressedImage::SharedPtr msg)
{
  LOGI("OnRGB: Received compressed image (%zu bytes)", msg->data.size());

  // Store latest RGB message
  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    latest_rgb_ = msg;
  }

  // Process frame if all 3 messages are available (matches Python timer approach)
  sensor_msgs::msg::CompressedImage::SharedPtr rgb;
  sensor_msgs::msg::Image::SharedPtr depth;
  sensor_msgs::msg::PointCloud2::SharedPtr cloud;

  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    if (latest_rgb_ && latest_depth_ && latest_cloud_)
    {
      rgb = latest_rgb_;
      depth = latest_depth_;
      cloud = latest_cloud_;
    }
  }

  if (rgb && depth && cloud)
  {
    ProcessFrame(rgb, depth, cloud);
  }
}

void PerceptionController::OnDepth(
    const sensor_msgs::msg::Image::SharedPtr msg)
{
  LOGI("OnDepth: Received depth image (%ux%u)", msg->width, msg->height);

  std::lock_guard<std::mutex> lock(latest_mutex_);
  latest_depth_ = msg;
}

void PerceptionController::OnPointCloud(
    const sensor_msgs::msg::PointCloud2::SharedPtr msg)
{
  LOGI("OnPointCloud: Received point cloud (%ux%u, %u points)",
       msg->width, msg->height, msg->width * msg->height);

  std::lock_guard<std::mutex> lock(latest_mutex_);
  latest_cloud_ = msg;
}

// ============================================================================
// Frame Processing
// ============================================================================

void PerceptionController::ProcessFrame(
    const sensor_msgs::msg::CompressedImage::SharedPtr &rgb,
    const sensor_msgs::msg::Image::SharedPtr &depth,
    const sensor_msgs::msg::PointCloud2::SharedPtr &cloud)
{
  LOGI("ProcessFrame: Starting inference (RGB size: %zu bytes)",
       rgb->data.size());

  // Decompress JPEG using TurboJPEG
  tjhandle decompressor = tjInitDecompress();
  if (!decompressor)
  {
    LOGW("Failed to initialize TurboJPEG decompressor");
    return;
  }

  int width, height, jpegSubsamp, jpegColorspace;
  int tj_result = tjDecompressHeader3(
      decompressor,
      rgb->data.data(),
      rgb->data.size(),
      &width,
      &height,
      &jpegSubsamp,
      &jpegColorspace);

  if (tj_result != 0)
  {
    LOGW("Failed to read JPEG header: %s", tjGetErrorStr2(decompressor));
    tjDestroy(decompressor);
    return;
  }

  // Allocate RGB buffer
  std::vector<uint8_t> rgb_buffer(width * height * 3);

  tj_result = tjDecompress2(
      decompressor,
      rgb->data.data(),
      rgb->data.size(),
      rgb_buffer.data(),
      width,
      0, // pitch (0 = use width * pixel_size)
      height,
      TJPF_BGR, // Decompress as BGR to match Python cv_bridge output
      TJFLAG_FASTDCT);

  tjDestroy(decompressor);

  if (tj_result != 0)
  {
    LOGW("Failed to decompress JPEG");
    return;
  }

  LOGI("Decompressed JPEG: %dx%d (%zu bytes RGB buffer)",
       width, height, rgb_buffer.size());

  // Run NCNN inference (YOLOv9 + Deep SORT) with raw RGB buffer
  auto start = std::chrono::high_resolution_clock::now();
  auto tracks = detector_->ProcessFrame(
      rgb_buffer.data(), width, height, kConfidenceThreshold, kIouThreshold);
  auto end = std::chrono::high_resolution_clock::now();

  double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

  // Get all tracks (including tentative) to see if YOLO is detecting anything
  size_t total_track_count = detector_->GetTrackCount();
  size_t confirmed_tracks = tracks.size();

  LOGI("Inference: %zu confirmed tracks (total %zu), %.1f ms (%.1f FPS)",
       confirmed_tracks, total_track_count, elapsed_ms, 1000.0 / elapsed_ms);

  // Update statistics
  total_detections_ += tracks.size();
  active_tracks_ = tracks.size();

  // Process each detected track
  for (const auto &track : tracks)
  {
    // Convert bbox to Rect
    Rect bbox(
        static_cast<int>(track.bbox[0]),
        static_cast<int>(track.bbox[1]),
        static_cast<int>(track.bbox[2] - track.bbox[0]),
        static_cast<int>(track.bbox[3] - track.bbox[1]));

    // Get 3D location from point cloud
    Point3f point3d = Get3DLocation(bbox, *cloud);

    // Crop point cloud for this detection
    auto cropped_cloud = CropPointCloud(bbox, *cloud);

    // Publish results
    PublishDetection(track, point3d, std::move(cropped_cloud), rgb->header);
  }
}

// ============================================================================
// 3D Localization
// ============================================================================

Point3f PerceptionController::Get3DLocation(
    const Rect &bbox,
    const sensor_msgs::msg::PointCloud2 &cloud)
{

  // Find center of bbox
  int center_x = bbox.x + bbox.width / 2;
  int center_y = bbox.y + bbox.height / 2;

  // Clamp to image bounds
  center_x = std::max(0, std::min(center_x, static_cast<int>(cloud.width) - 1));
  center_y = std::max(0, std::min(center_y, static_cast<int>(cloud.height) - 1));

  // Find field offsets for x, y, z
  int x_offset = -1, y_offset = -1, z_offset = -1;
  for (const auto &field : cloud.fields)
  {
    if (field.name == "x")
      x_offset = field.offset;
    if (field.name == "y")
      y_offset = field.offset;
    if (field.name == "z")
      z_offset = field.offset;
  }

  if (x_offset < 0 || y_offset < 0 || z_offset < 0)
  {
    LOGW("Point cloud missing x/y/z fields");
    return Point3f(NAN, NAN, NAN);
  }

  // Calculate data index
  size_t index = center_y * cloud.row_step + center_x * cloud.point_step;

  if (index + z_offset + sizeof(float) > cloud.data.size())
  {
    LOGW("Point cloud index out of bounds");
    return Point3f(NAN, NAN, NAN);
  }

  // Extract XYZ (assumes float32)
  float x = *reinterpret_cast<const float *>(&cloud.data[index + x_offset]);
  float y = *reinterpret_cast<const float *>(&cloud.data[index + y_offset]);
  float z = *reinterpret_cast<const float *>(&cloud.data[index + z_offset]);

  // Check for invalid points
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
  {
    LOGD("Invalid point at bbox center (%d, %d)", center_x, center_y);
    return Point3f(NAN, NAN, NAN);
  }

  return Point3f(x, y, z);
}

sensor_msgs::msg::PointCloud2::UniquePtr PerceptionController::CropPointCloud(
    const Rect &bbox,
    const sensor_msgs::msg::PointCloud2 &cloud)
{

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

  if (crop_width <= 0 || crop_height <= 0)
  {
    LOGW("Invalid bbox for cropping");
    return nullptr;
  }

  cropped->width = crop_width;
  cropped->height = crop_height;
  cropped->row_step = crop_width * cloud.point_step;

  // Reserve space
  cropped->data.reserve(crop_height * cropped->row_step);

  // Extract points within bbox
  for (int y = y1; y < y2; ++y)
  {
    for (int x = x1; x < x2; ++x)
    {
      size_t src_index = y * cloud.row_step + x * cloud.point_step;

      if (src_index + cloud.point_step <= cloud.data.size())
      {
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
    const perception::Track &track,
    const Point3f &point3d,
    sensor_msgs::msg::PointCloud2::UniquePtr cropped_cloud,
    const std_msgs::msg::Header &header)
{

  // Publish Point (center location)
  if (std::isfinite(point3d.x) && std::isfinite(point3d.y) && std::isfinite(point3d.z))
  {
    geometry_msgs::msg::Point point_msg;
    point_msg.x = point3d.x;
    point_msg.y = point3d.y;
    point_msg.z = point3d.z;

    if (track.class_id == 0)
    {
      pub_beetle_center_.Publish(point_msg);
    }
    else if (track.class_id == 1)
    {
      pub_larva_center_.Publish(point_msg);
    }
    else if (track.class_id == 2)
    {
      pub_eggs_center_.Publish(point_msg);
    }
  }

  // Publish PointCloud2 (cropped region)
  if (cropped_cloud && !cropped_cloud->data.empty())
  {
    cropped_cloud->header = header;

    if (track.class_id == 0)
    {
      pub_beetle_.Publish(std::move(cropped_cloud));
    }
    else if (track.class_id == 1)
    {
      pub_larva_.Publish(std::move(cropped_cloud));
    }
    else if (track.class_id == 2)
    {
      pub_eggs_.Publish(std::move(cropped_cloud));
    }
  }
}
