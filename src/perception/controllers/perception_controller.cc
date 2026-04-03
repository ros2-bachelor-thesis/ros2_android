#include "perception/controllers/perception_controller.h"

#include <sstream>
#include <cmath>
#include <turbojpeg.h>

#include <perception/types.h>

#include "core/log.h"
#include "core/notification_queue.h"
#include "core/debug_frame_callback_queue.h"

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
  constexpr float kConfidenceThreshold = 0.5f; // Matches Python line 245
  constexpr float kIouThreshold = 0.45f;

  // ZED camera resolution constants (hardcoded for ZED2)
  // Python parameters (object_detection.py lines 45-47):
  // - img_size_y: 720 (RGB height)
  // - pointcloud_size: 448 (point cloud width)
  // - pointcloud_size_y: 256 (point cloud height)
  constexpr int RGB_WIDTH = 1280;
  constexpr int RGB_HEIGHT = 720;
  constexpr int CLOUD_WIDTH = 448;
  constexpr int CLOUD_HEIGHT = 256;

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

  LOGD("Initializing PerceptionController with models from: %s", models_path.c_str());

  // Build model file paths
  std::string yolo_param = models_path + "/yolov9_s_pobed.ncnn.param";
  std::string yolo_bin = models_path + "/yolov9_s_pobed.ncnn.bin";
  std::string reid_param = models_path + "/osnet_ain_x1_0.ncnn.param";
  std::string reid_bin = models_path + "/osnet_ain_x1_0.ncnn.bin";

  // Initialize NCNN detector with Vulkan auto-detection
  // Will automatically fall back to CPU if Vulkan is not available
  detector_ = std::make_unique<perception::ObjectDetectionController>(
      yolo_param, yolo_bin, reid_param, reid_bin, true);

  if (!detector_->IsReady())
  {
    LOGE("Failed to load perception models from %s", models_path.c_str());
    PostNotification(NotificationSeverity::ERROR,
                     "Failed to load perception models");
    return;
  }

  LOGD("Perception models loaded successfully");

  pub_beetle_center_.SetTopic("cpb_beetle_center");
  pub_beetle_.SetTopic("cpb_beetle");
  pub_larva_center_.SetTopic("cpb_larva_center");
  pub_larva_.SetTopic("cpb_larva");
  pub_eggs_center_.SetTopic("cpb_eggs_center");
  pub_eggs_.SetTopic("cpb_eggs");

  // Use QoS() reliable matching Python reference
  auto qos = rclcpp::QoS(rclcpp::KeepLast(10))
                 .reliable()
                 .durability_volatile();
  pub_beetle_center_.SetQos(qos);
  pub_beetle_.SetQos(qos);
  pub_larva_center_.SetQos(qos);
  pub_larva_.SetQos(qos);
  pub_eggs_center_.SetQos(qos);
  pub_eggs_.SetQos(qos);

  LOGD("PerceptionController initialized");
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
  return enabled_ ? "{\"enabled\":true}" : "{\"enabled\":false}";
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

  LOGD("Enabling PerceptionController");

  auto node = ros_.get_node();
  if (!node)
  {
    LOGE("ROS node not initialized");
    return;
  }

  // Currently used for the heavy processing of depth and point cloud
  auto qos = rclcpp::QoS(rclcpp::KeepLast(10))
                 .reliable()
                 .durability_volatile();

  rgb_sub_ = node->create_subscription<sensor_msgs::msg::CompressedImage>(
      "/zed/zed_node/rgb/image_rect_color/compressed", qos,
      std::bind(&PerceptionController::OnRGB, this, std::placeholders::_1));
  LOGD("  RGB subscription publisher count: %zu", rgb_sub_->get_publisher_count());

  depth_sub_ = node->create_subscription<sensor_msgs::msg::Image>(
      "/zed/zed_node/depth/depth_registered", qos,
      std::bind(&PerceptionController::OnDepth, this, std::placeholders::_1));
  LOGD("  Depth subscription publisher count: %zu", depth_sub_->get_publisher_count());

  cloud_sub_ = node->create_subscription<sensor_msgs::msg::PointCloud2>(
      "/zed/zed_node/point_cloud/cloud_registered", qos,
      std::bind(&PerceptionController::OnPointCloud, this, std::placeholders::_1));
  LOGD("  Cloud subscription publisher count: %zu", cloud_sub_->get_publisher_count());

  // Create 20Hz timer (matches Python line 79)
  timer_ = node->create_wall_timer(
      std::chrono::milliseconds(1000 / kFrequencyHz),
      std::bind(&PerceptionController::TimerCallback, this));

  // Reset camera flags
  camera_rgb_ = false;
  camera_depth_ = false;
  camera_pointcloud_ = false;

  // Enable publishers
  pub_beetle_center_.Enable();
  pub_beetle_.Enable();
  pub_larva_center_.Enable();
  pub_larva_.Enable();
  pub_eggs_center_.Enable();
  pub_eggs_.Enable();

  enabled_ = true;

  LOGI("PerceptionController enabled - 20Hz timer active");
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

  // Stop timer
  timer_.reset();

  // Reset flags
  camera_rgb_ = false;
  camera_depth_ = false;
  camera_pointcloud_ = false;

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
  std::lock_guard<std::mutex> lock(latest_mutex_);
  latest_rgb_ = msg;

  if (!camera_rgb_)
  {
    LOGI("First RGB message received (%zu bytes)", msg->data.size());
    camera_rgb_ = true;
  }
}

void PerceptionController::OnDepth(
    const sensor_msgs::msg::Image::SharedPtr msg)
{

  std::lock_guard<std::mutex> lock(latest_mutex_);
  latest_depth_ = msg;

  if (!camera_depth_)
  {
    LOGI("First depth message received (%ux%u)", msg->width, msg->height);
    camera_depth_ = true;
  }
}

void PerceptionController::OnPointCloud(
    const sensor_msgs::msg::PointCloud2::SharedPtr msg)
{

  std::lock_guard<std::mutex> lock(latest_mutex_);
  latest_cloud_ = msg;

  if (!camera_pointcloud_)
  {
    LOGI("First cloud message received (%ux%u)", msg->width, msg->height);
    camera_pointcloud_ = true;
  }
}

void PerceptionController::TimerCallback()
{
  // Debug: Log timer activity every second
  static int tick_count = 0;
  if (++tick_count % 20 == 0)
  {
    LOGD("Timer tick #%d: rgb=%d depth=%d cloud=%d",
         tick_count, camera_rgb_.load(), camera_depth_.load(),
         camera_pointcloud_.load());
  }

  // Gate on RGB availability (matches Python line 385: if self.camera_RGB == True)
  if (!camera_rgb_)
  {
    return;
  }

  // Copy latest messages under lock
  sensor_msgs::msg::CompressedImage::SharedPtr rgb;
  sensor_msgs::msg::Image::SharedPtr depth;
  sensor_msgs::msg::PointCloud2::SharedPtr cloud;

  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    rgb = latest_rgb_;
    depth = latest_depth_;
    cloud = latest_cloud_;
  }

  // Check if we have RGB (required)
  if (!rgb)
  {
    return;
  }

  // Check timestamp to avoid reprocessing same message (fixes infinite loop)
  if (rgb->header.stamp == last_processed_rgb_stamp_)
  {
    return; // Already processed this frame
  }

  // Update timestamp
  last_processed_rgb_stamp_ = rgb->header.stamp;

  // Process frame (depth and cloud are optional, matching Python behavior)
  // Python always processes RGB, conditionally uses depth+cloud
  ProcessFrame(rgb, depth, cloud);
}

// ============================================================================
// Frame Processing
// ============================================================================

void PerceptionController::ProcessFrame(
    const sensor_msgs::msg::CompressedImage::SharedPtr &rgb,
    const sensor_msgs::msg::Image::SharedPtr &depth,
    const sensor_msgs::msg::PointCloud2::SharedPtr &cloud)
{
  // Log entry with optional depth/cloud status
  if (depth && cloud)
  {
    LOGD("ProcessFrame: RGB + Depth + Cloud available");
  }
  else
  {
    LOGD("ProcessFrame: RGB only (depth/cloud unavailable)");
  }

  // =========================================================================
  // Step 1: Decompress JPEG to BGR
  // =========================================================================

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

  // Allocate BGR buffer
  std::vector<uint8_t> bgr_buffer(width * height * 3);

  tj_result = tjDecompress2(
      decompressor,
      rgb->data.data(),
      rgb->data.size(),
      bgr_buffer.data(),
      width,
      0, // pitch (0 = use width * pixel_size)
      height,
      TJPF_BGR, // Decompress as BGR (OpenCV default)
      TJFLAG_FASTDCT);

  tjDestroy(decompressor);

  if (tj_result != 0)
  {
    LOGW("Failed to decompress JPEG");
    return;
  }

  LOGD("Decompressed JPEG: %dx%d", width, height);

  // =========================================================================
  // Step 2: Prepare depth data pointer (optional)
  // =========================================================================

  const float *depth_data = nullptr;
  int depth_width = 0;
  int depth_height = 0;

  if (depth)
  {
    // Depth image format: 32FC1 (float32, single channel, meters)
    if (depth->encoding == "32FC1")
    {
      depth_data = reinterpret_cast<const float *>(depth->data.data());
      depth_width = depth->width;
      depth_height = depth->height;
    }
    else
    {
      LOGW("Unexpected depth encoding: %s (expected 32FC1)", depth->encoding.c_str());
    }
  }

  // =========================================================================
  // Step 3: Run perception pipeline (YOLO + Deep SORT + visualization)
  // =========================================================================

  auto start = std::chrono::high_resolution_clock::now();
  perception::PerceptionResult result = detector_->ProcessFrame(
      bgr_buffer.data(), width, height,
      depth_data, depth_width, depth_height,
      kConfidenceThreshold, kIouThreshold, false); // TODO (ohagenauer) enable tracking once correctly implemented in ros2_android_perception
  auto end = std::chrono::high_resolution_clock::now();

  double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

  LOGI("Perception: %zu detections, %zu tracks, %.1f ms (%.1f FPS)",
       result.detections.size(), result.tracks.size(),
       elapsed_ms, 1000.0 / elapsed_ms);

  // =========================================================================
  // Step 4: Publish detections ONLY if depth+cloud available (matches Python)
  // =========================================================================

  if (depth && cloud)
  {
    LOGD("Publishing %zu detections (depth+cloud available)", result.detections.size());

    for (const auto &det : result.detections)
    {
      // Convert Detection to Rect (bbox is [x1, y1, x2, y2])
      Rect bbox(
          static_cast<int>(det.bbox[0]),
          static_cast<int>(det.bbox[1]),
          static_cast<int>(det.bbox[2] - det.bbox[0]),  // width
          static_cast<int>(det.bbox[3] - det.bbox[1])); // height

      // Get 3D location from point cloud (with RGB→cloud scaling)
      Point3f point3d = Get3DLocation(bbox, *cloud);

      // Crop point cloud for this detection (with depth filtering)
      auto cropped_cloud = CropPointCloud(bbox, *cloud, *depth);

      // Publish detection result
      PublishDetection(det, point3d, std::move(cropped_cloud), rgb->header);
    }
  }
  else
  {
    LOGD("Skipping publish (depth/cloud unavailable)");
  }

  // =========================================================================
  // Step 5: Store debug frames for JNI retrieval (if visualization enabled)
  // =========================================================================

  if (visualization_enabled_)
  {
    // Helper lambda to encode raw BGR buffer as JPEG
    auto encode_jpeg = [](const uint8_t *bgr_data, int img_width, int img_height) -> std::vector<uint8_t>
    {
      if (!bgr_data || img_width <= 0 || img_height <= 0)
      {
        return {};
      }

      tjhandle compressor = tjInitCompress();
      if (!compressor)
      {
        LOGW("Failed to init TurboJPEG compressor");
        return {};
      }

      unsigned char *jpeg_buf = nullptr;
      unsigned long jpeg_size = 0;

      int tj_result = tjCompress2(
          compressor,
          const_cast<uint8_t *>(bgr_data),
          img_width,
          0, // pitch (0 = width * pixel_size)
          img_height,
          TJPF_BGR,
          &jpeg_buf,
          &jpeg_size,
          TJSAMP_420, // 4:2:0 chroma subsampling
          85,         // JPEG quality (85 = good balance)
          TJFLAG_FASTDCT);

      std::vector<uint8_t> result_jpeg;
      if (tj_result == 0 && jpeg_buf && jpeg_size > 0)
      {
        result_jpeg.assign(jpeg_buf, jpeg_buf + jpeg_size);
      }

      if (jpeg_buf)
      {
        tjFree(jpeg_buf);
      }
      tjDestroy(compressor);

      return result_jpeg;
    };

    // Encode and store RGB annotated frame
    if (!result.annotated_rgb_bgr.empty())
    {
      auto rgb_jpeg = encode_jpeg(result.annotated_rgb_bgr.data(),
                                  result.rgb_width, result.rgb_height);
      if (!rgb_jpeg.empty())
      {
        {
          std::lock_guard<std::mutex> lock(debug_frames_mutex_);
          debug_frames_jpeg_["rgb_annotated"] = std::move(rgb_jpeg);
        }
        PostDebugFrameUpdate("rgb_annotated");
        LOGD("Stored RGB annotated frame (%zu KB)", rgb_jpeg.size() / 1024);
      }
    }

    // Encode and store depth annotated frame (if available)
    if (result.has_depth_visualization && !result.annotated_depth_bgr.empty())
    {
      auto depth_jpeg = encode_jpeg(result.annotated_depth_bgr.data(),
                                    result.depth_width, result.depth_height);
      if (!depth_jpeg.empty())
      {
        {
          std::lock_guard<std::mutex> lock(debug_frames_mutex_);
          debug_frames_jpeg_["depth_annotated"] = std::move(depth_jpeg);
        }
        PostDebugFrameUpdate("depth_annotated");
        LOGD("Stored depth annotated frame (%zu KB)", depth_jpeg.size() / 1024);
      }
    }
  }
}

// ============================================================================
// 3D Localization
// ============================================================================

Point3f PerceptionController::Get3DLocation(
    const Rect &bbox,
    const sensor_msgs::msg::PointCloud2 &cloud)
{

  // Find center of bbox (in RGB coordinates 1280×720)
  int rgb_center_x = bbox.x + bbox.width / 2;
  int rgb_center_y = bbox.y + bbox.height / 2;

  // Scale from RGB space (1280×720) to point cloud space (448×256)
  // Python code (line 310):
  // int(y*rgb_to_pointcloud_factor_y+x//rgb_to_pointcloud_factor_x)
  // where rgb_to_pointcloud_factor_y = pointcloud_size/image_to_pointcloud_factor_y
  //       rgb_to_pointcloud_factor_x = img_size_y/pointcloud_size_y
  //
  // Simplified: scale_x = CLOUD_WIDTH / RGB_WIDTH, scale_y = CLOUD_HEIGHT / RGB_HEIGHT
  float scale_x = static_cast<float>(CLOUD_WIDTH) / RGB_WIDTH;
  float scale_y = static_cast<float>(CLOUD_HEIGHT) / RGB_HEIGHT;

  int center_x = static_cast<int>(rgb_center_x * scale_x);
  int center_y = static_cast<int>(rgb_center_y * scale_y);

  // Clamp to cloud bounds
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

  LOGD("3D location: [%.3f, %.3f, %.3f] at pixel (%d, %d)",
       x, y, z, center_x, center_y);
  return Point3f(x, y, z);
}

sensor_msgs::msg::PointCloud2::UniquePtr PerceptionController::CropPointCloud(
    const Rect &bbox,
    const sensor_msgs::msg::PointCloud2 &cloud,
    const sensor_msgs::msg::Image &depth)
{
  auto cropped = std::make_unique<sensor_msgs::msg::PointCloud2>();
  cropped->header = cloud.header;
  cropped->fields = cloud.fields;
  cropped->is_bigendian = cloud.is_bigendian;
  cropped->point_step = cloud.point_step;
  cropped->is_dense = false;

  // Scale bbox from RGB space (1280×720) to point cloud space (448×256)
  float scale_x = static_cast<float>(CLOUD_WIDTH) / RGB_WIDTH;
  float scale_y = static_cast<float>(CLOUD_HEIGHT) / RGB_HEIGHT;

  int scaled_x1 = static_cast<int>(bbox.x * scale_x);
  int scaled_y1 = static_cast<int>(bbox.y * scale_y);
  int scaled_x2 = static_cast<int>((bbox.x + bbox.width) * scale_x);
  int scaled_y2 = static_cast<int>((bbox.y + bbox.height) * scale_y);

  // Clamp bbox to cloud bounds
  int x1 = std::max(0, scaled_x1);
  int y1 = std::max(0, scaled_y1);
  int x2 = std::min(static_cast<int>(cloud.width), scaled_x2);
  int y2 = std::min(static_cast<int>(cloud.height), scaled_y2);

  int crop_width = x2 - x1;
  int crop_height = y2 - y1;

  if (crop_width <= 0 || crop_height <= 0)
  {
    LOGW("Invalid bbox for cropping");
    return nullptr;
  }

  // Calculate median depth in bbox (Python line 288)
  std::vector<float> depth_values;
  depth_values.reserve(crop_width * crop_height);

  const float *depth_data = reinterpret_cast<const float *>(depth.data.data());
  for (int y = y1; y < y2; ++y)
  {
    for (int x = x1; x < x2; ++x)
    {
      float d = depth_data[y * depth.width + x];
      if (std::isfinite(d) && d > 0.0f)
      {
        depth_values.push_back(d);
      }
    }
  }

  if (depth_values.empty())
  {
    LOGW("No valid depth values in bbox");
    return nullptr;
  }

  // Median calculation
  std::nth_element(depth_values.begin(),
                   depth_values.begin() + depth_values.size() / 2,
                   depth_values.end());
  float median_depth = depth_values[depth_values.size() / 2];

  // Python lines 302-305: Filter points within ±10% median, skip NaN/inf/> 5m
  float min_depth = 0.9f * median_depth;
  float max_depth = 1.1f * median_depth;

  cropped->width = crop_width;
  cropped->height = crop_height;
  cropped->row_step = crop_width * cloud.point_step;
  cropped->data.reserve(crop_height * cropped->row_step);

  // Find XYZ field offsets
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

  // Extract filtered points
  for (int y = y1; y < y2; ++y)
  {
    for (int x = x1; x < x2; ++x)
    {
      float pixel_depth = depth_data[y * depth.width + x];

      // Python filter logic (lines 302-305)
      if (pixel_depth < min_depth || pixel_depth > max_depth)
        continue;
      if (!std::isfinite(pixel_depth) || pixel_depth > 5.0f)
        continue;

      // Check if point cloud point is valid
      size_t src_index = y * cloud.row_step + x * cloud.point_step;
      if (x_offset >= 0 && src_index + z_offset + sizeof(float) <= cloud.data.size())
      {
        float z = *reinterpret_cast<const float *>(&cloud.data[src_index + z_offset]);
        if (!std::isfinite(z))
          continue;
      }

      // Add point to cropped cloud
      if (src_index + cloud.point_step <= cloud.data.size())
      {
        cropped->data.insert(cropped->data.end(),
                             cloud.data.begin() + src_index,
                             cloud.data.begin() + src_index + cloud.point_step);
      }
    }
  }

  LOGD("Cropped cloud: %ux%u, %zu points filtered",
       cropped->width, cropped->height, cropped->data.size() / cloud.point_step);
  return cropped;
}

// ============================================================================
// Publishing and Logging
// ============================================================================

void PerceptionController::PublishDetection(
    const perception::Detection &det,
    const Point3f &point3d,
    sensor_msgs::msg::PointCloud2::UniquePtr cropped_cloud,
    const std_msgs::msg::Header &header)
{
  LOGD("Publishing detection (class %d, conf=%.2f): 3D=[%.2f,%.2f,%.2f] cloud_size=%zu",
       det.class_id, det.confidence, point3d.x, point3d.y, point3d.z,
       cropped_cloud ? cropped_cloud->data.size() : 0);

  // Publish Point (center location) - only if < 2m (Python line 337)
  if (std::isfinite(point3d.x) && std::isfinite(point3d.y) &&
      std::isfinite(point3d.z) && point3d.z < 2.0f)
  {
    geometry_msgs::msg::Point point_msg;
    point_msg.x = point3d.x;
    point_msg.y = point3d.y;
    point_msg.z = point3d.z;

    if (det.class_id == 0)
    {
      pub_beetle_center_.Publish(point_msg);
    }
    else if (det.class_id == 1)
    {
      pub_larva_center_.Publish(point_msg);
    }
    else if (det.class_id == 2)
    {
      pub_eggs_center_.Publish(point_msg);
    }
  }

  // Publish PointCloud2 (cropped region)
  if (cropped_cloud && !cropped_cloud->data.empty())
  {
    cropped_cloud->header = header;

    if (det.class_id == 0)
    {
      pub_beetle_.Publish(std::move(cropped_cloud));
    }
    else if (det.class_id == 1)
    {
      pub_larva_.Publish(std::move(cropped_cloud));
    }
    else if (det.class_id == 2)
    {
      pub_eggs_.Publish(std::move(cropped_cloud));
    }
  }
}

bool PerceptionController::GetDebugFrame(const std::string &frame_id, std::vector<uint8_t> &out_jpeg)
{
  std::lock_guard<std::mutex> lock(debug_frames_mutex_);

  auto it = debug_frames_jpeg_.find(frame_id);
  if (it == debug_frames_jpeg_.end() || it->second.empty())
  {
    return false;
  }

  out_jpeg = it->second;
  return true;
}
