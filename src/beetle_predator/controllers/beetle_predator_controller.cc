#include "beetle_predator/controllers/beetle_predator_controller.h"

#include <chrono>
#include <cmath>
#include <sstream>
#include <thread>
#include <turbojpeg.h>

#include "core/debug_frame_callback_queue.h"
#include "core/log.h"
#include "perception/types.h"

namespace ros2_android
{

  static const char *kClassNames[] = {"cpb_beetle", "cpb_larva", "cpb_eggs"};

  BeetlePredatorController::BeetlePredatorController(
      RosInterface &ros,
      const std::string &models_path,
      CameraController *rear_camera,
      GpsLocationProvider *gps_provider)
      : SensorDataProvider("beetle_predator"),
        ros_(ros),
        rear_camera_(rear_camera),
        gps_provider_(gps_provider),
        models_path_(models_path),
        detection_pub_(ros)
  {
    std::string yolo_param = models_path_ + "/yolov9_s_pobed.ncnn.param";
    std::string yolo_bin = models_path_ + "/yolov9_s_pobed.ncnn.bin";
    std::string reid_param = models_path_ + "/osnet_ain_x1_0.ncnn.param";
    std::string reid_bin = models_path_ + "/osnet_ain_x1_0.ncnn.bin";

    detector_ = std::make_unique<perception::ObjectDetectionController>(
        yolo_param, yolo_bin, reid_param, reid_bin);

    if (!detector_->IsReady())
    {
      LOGE("BeetlePredator: Failed to load NCNN models from %s", models_path_.c_str());
    }
    else
    {
      LOGI("BeetlePredator: NCNN models loaded (snapshot mode)");
    }

    detection_pub_.SetTopic("cpb_predator/detection");
  }

  BeetlePredatorController::~BeetlePredatorController()
  {
    if (enabled_)
    {
      Disable();
    }
  }

  std::string BeetlePredatorController::PrettyName() const
  {
    return "Beetle Predator";
  }

  std::string BeetlePredatorController::GetLastMeasurementJson()
  {
    return "{}";
  }

  bool BeetlePredatorController::GetLastMeasurement(jni::SensorReadingData &out_data)
  {
    return false;
  }

  void BeetlePredatorController::Enable()
  {
    if (enabled_)
      return;

    if (!detector_ || !detector_->IsReady())
    {
      LOGE("BeetlePredator: Cannot enable - models not loaded");
      return;
    }

    if (!rear_camera_)
    {
      LOGE("BeetlePredator: Cannot enable - no rear camera available");
      return;
    }

    if (!rear_camera_->IsEnabled())
    {
      LOGI("BeetlePredator: Auto-enabling rear camera");
      rear_camera_->EnableCamera();
    }

    new_detection_count_.store(0);
    detection_pub_.Enable();
    enabled_ = true;

    StartPreviewThread();

    LOGI("BeetlePredator: Enabled in snapshot mode (label_mask=0x%02x)", label_mask_.load());
  }

  void BeetlePredatorController::Disable()
  {
    if (!enabled_)
      return;

    enabled_ = false;
    StopPreviewThread();

    detection_pub_.Disable();

    if (rear_camera_ && rear_camera_->IsEnabled())
    {
      LOGI("BeetlePredator: Disabling rear camera");
      rear_camera_->DisableCamera();
    }

    {
      std::lock_guard<std::mutex> lock(debug_frames_mutex_);
      debug_frames_jpeg_.clear();
    }

    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      last_snapshot_ = SnapshotResult{};
    }

    LOGI("BeetlePredator: Disabled (published %d detections)", new_detection_count_.load());
  }

  void BeetlePredatorController::StartPreviewThread()
  {
    StopPreviewThread();
    preview_running_.store(true);
    preview_thread_ = std::thread([this]()
    {
      while (preview_running_.load())
      {
        std::vector<uint8_t> rgba_data;
        int width = 0, height = 0;
        if (rear_camera_->GetLastFrame(rgba_data, width, height) && !rgba_data.empty())
        {
          size_t pixel_count = static_cast<size_t>(width) * height;
          std::vector<uint8_t> bgr_data(pixel_count * 3);
          for (size_t i = 0; i < pixel_count; i++)
          {
            bgr_data[i * 3 + 0] = rgba_data[i * 4 + 2];
            bgr_data[i * 3 + 1] = rgba_data[i * 4 + 1];
            bgr_data[i * 3 + 2] = rgba_data[i * 4 + 0];
          }
          EncodeAndPostFrame(bgr_data, width, height, "beetle_predator_rgb");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
  }

  void BeetlePredatorController::StopPreviewThread()
  {
    preview_running_.store(false);
    if (preview_thread_.joinable())
    {
      preview_thread_.join();
    }
  }

  void BeetlePredatorController::EncodeAndPostFrame(
      const std::vector<uint8_t> &bgr_data, int width, int height,
      const std::string &frame_id)
  {
    if (bgr_data.empty() || width <= 0 || height <= 0)
      return;

    tjhandle compressor = tjInitCompress();
    if (!compressor)
      return;

    unsigned char *jpeg_buf = nullptr;
    unsigned long jpeg_size = 0;

    int ret = tjCompress2(
        compressor,
        const_cast<uint8_t *>(bgr_data.data()),
        width, 0, height, TJPF_BGR,
        &jpeg_buf, &jpeg_size,
        TJSAMP_420, 85, TJFLAG_FASTDCT);

    if (ret == 0 && jpeg_buf && jpeg_size > 0)
    {
      {
        std::lock_guard<std::mutex> lock(debug_frames_mutex_);
        debug_frames_jpeg_[frame_id] = std::vector<uint8_t>(jpeg_buf, jpeg_buf + jpeg_size);
      }
      PostDebugFrameUpdate(frame_id);
    }

    if (jpeg_buf)
      tjFree(jpeg_buf);
    tjDestroy(compressor);
  }

  void BeetlePredatorController::TakeSnapshot()
  {
    if (!enabled_ || processing_.exchange(true))
      return;

    // Stop preview so the snapshot result is the only frame posted
    StopPreviewThread();

    auto t_start = std::chrono::steady_clock::now();

    // 1. Capture camera frame
    std::vector<uint8_t> rgba_data;
    int width = 0, height = 0;
    if (!rear_camera_->GetLastFrame(rgba_data, width, height) || rgba_data.empty())
    {
      LOGE("BeetlePredator: TakeSnapshot - no camera frame available");
      processing_.store(false);
      StartPreviewThread();
      return;
    }

    // 2. Capture GPS at this exact moment
    sensor_msgs::msg::NavSatFix gps_fix;
    bool has_gps = gps_provider_ ? gps_provider_->GetLastLocation(gps_fix) : false;

    // 3. RGBA -> BGR
    size_t pixel_count = static_cast<size_t>(width) * height;
    std::vector<uint8_t> bgr_data(pixel_count * 3);
    for (size_t i = 0; i < pixel_count; i++)
    {
      bgr_data[i * 3 + 0] = rgba_data[i * 4 + 2];
      bgr_data[i * 3 + 1] = rgba_data[i * 4 + 1];
      bgr_data[i * 3 + 2] = rgba_data[i * 4 + 0];
    }

    auto t_convert = std::chrono::steady_clock::now();

    // 4. Run YOLO detection without Deep SORT tracking (single-frame, no temporal continuity)
    perception::PerceptionResult result = detector_->ProcessFrame(
        bgr_data.data(), width, height,
        nullptr, 0, 0,
        0.5f, 0.45f,
        false);

    auto t_inference = std::chrono::steady_clock::now();

    // 5. Build snapshot result and publish detections
    uint8_t mask = label_mask_.load();
    auto node = ros_.get_node();

    SnapshotResult snapshot;
    snapshot.has_gps = has_gps;
    if (has_gps)
    {
      snapshot.lat = gps_fix.latitude;
      snapshot.lon = gps_fix.longitude;
      snapshot.alt = gps_fix.altitude;
      snapshot.accuracy = static_cast<float>(std::sqrt(gps_fix.position_covariance[0]));
    }

    int seq_id = 0;
    for (const auto &det : result.detections)
    {
      if (det.class_id < 0 || det.class_id > 2)
        continue;
      if (!(mask & (1 << det.class_id)))
        continue;

      vermin_collector_ros_msgs::msg::BeetleDetection msg;
      msg.header.stamp = node->now();
      msg.header.frame_id = "beetle_predator";

      if (has_gps)
      {
        msg.latitude = gps_fix.latitude;
        msg.longitude = gps_fix.longitude;
        msg.altitude = gps_fix.altitude;
        msg.horizontal_accuracy = snapshot.accuracy;
      }
      else
      {
        msg.latitude = 0.0;
        msg.longitude = 0.0;
        msg.altitude = 0.0;
        msg.horizontal_accuracy = -1.0f;
      }

      msg.label = kClassNames[det.class_id];
      msg.class_id = det.class_id;
      msg.confidence = det.confidence;
      msg.track_id = seq_id++;

      msg.bbox_x = static_cast<int>(det.bbox[0]);
      msg.bbox_y = static_cast<int>(det.bbox[1]);
      msg.bbox_width = static_cast<int>(det.bbox[2] - det.bbox[0]);
      msg.bbox_height = static_cast<int>(det.bbox[3] - det.bbox[1]);
      msg.frame_width = width;
      msg.frame_height = height;

      detection_pub_.Publish(msg);
      new_detection_count_.fetch_add(1);

      SnapshotEntry entry;
      entry.label = msg.label;
      entry.class_id = det.class_id;
      entry.confidence = det.confidence;
      entry.bbox_x = msg.bbox_x;
      entry.bbox_y = msg.bbox_y;
      entry.bbox_w = msg.bbox_width;
      entry.bbox_h = msg.bbox_height;
      snapshot.detections.push_back(entry);

      LOGI("BeetlePredator: Detected %s (conf=%.2f, gps=%s)",
           msg.label.c_str(), det.confidence, has_gps ? "yes" : "no");
    }

    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      last_snapshot_ = snapshot;
    }

    auto t_publish = std::chrono::steady_clock::now();

    // 6. Post annotated frame under snapshot key (triggers UI result display)
    const auto &annotated = result.annotated_rgb_bgr;
    if (!annotated.empty() && result.rgb_width > 0 && result.rgb_height > 0)
    {
      EncodeAndPostFrame(annotated, result.rgb_width, result.rgb_height, "beetle_predator_snapshot");
    }
    else
    {
      EncodeAndPostFrame(bgr_data, width, height, "beetle_predator_snapshot");
    }

    auto t_end = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b)
    {
      return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    LOGI("BeetlePredator: Snapshot done: convert=%lldms inference=%lldms total=%lldms detections=%zu",
         ms(t_start, t_convert), ms(t_convert, t_inference),
         ms(t_start, t_end), snapshot.detections.size());

    processing_.store(false);
    if (enabled_)
      StartPreviewThread();
  }

  std::string BeetlePredatorController::GetLastDetectionsJson() const
  {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);

    if (!last_snapshot_.has_gps && last_snapshot_.detections.empty())
    {
      return "{}";
    }

    std::ostringstream oss;
    oss << "{";
    oss << "\"latitude\":" << last_snapshot_.lat << ",";
    oss << "\"longitude\":" << last_snapshot_.lon << ",";
    oss << "\"altitude\":" << last_snapshot_.alt << ",";
    oss << "\"accuracy\":" << last_snapshot_.accuracy << ",";
    oss << "\"has_gps\":" << (last_snapshot_.has_gps ? "true" : "false") << ",";
    oss << "\"detections\":[";
    for (size_t i = 0; i < last_snapshot_.detections.size(); i++)
    {
      const auto &d = last_snapshot_.detections[i];
      if (i > 0)
        oss << ",";
      oss << "{";
      oss << "\"label\":\"" << d.label << "\",";
      oss << "\"class_id\":" << d.class_id << ",";
      oss << "\"confidence\":" << d.confidence << ",";
      oss << "\"bbox_x\":" << d.bbox_x << ",";
      oss << "\"bbox_y\":" << d.bbox_y << ",";
      oss << "\"bbox_w\":" << d.bbox_w << ",";
      oss << "\"bbox_h\":" << d.bbox_h;
      oss << "}";
    }
    oss << "]}";
    return oss.str();
  }

  bool BeetlePredatorController::GetDebugFrame(
      const std::string &frame_id, std::vector<uint8_t> &out_jpeg)
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

} // namespace ros2_android
