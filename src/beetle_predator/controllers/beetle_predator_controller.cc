#include "beetle_predator/controllers/beetle_predator_controller.h"

#include <chrono>
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
    // Initialize ML pipeline
    std::string yolo_param = models_path_ + "/yolov9_s_pobed.ncnn.param";
    std::string yolo_bin = models_path_ + "/yolov9_s_pobed.ncnn.bin";
    std::string reid_param = models_path_ + "/osnet_ain_x1_0.ncnn.param";
    std::string reid_bin = models_path_ + "/osnet_ain_x1_0.ncnn.bin";

    detector_ = std::make_unique<perception::ObjectDetectionController>(
        yolo_param, yolo_bin, reid_param, reid_bin);

    if (!detector_->IsReady())
    {
      LOGE("BeetlePredator: Failed to load NCNN models from %s",
           models_path_.c_str());
    }
    else
    {
      LOGI("BeetlePredator: NCNN models loaded successfully");
    }

    // Set up publisher topic
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

  bool BeetlePredatorController::GetLastMeasurement(
      jni::SensorReadingData &out_data)
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

    // Enable the rear camera if not already running
    if (!rear_camera_->IsEnabled())
    {
      LOGI("BeetlePredator: Auto-enabling rear camera");
      rear_camera_->EnableCamera();
    }

    // Reset novelty filter
    {
      std::lock_guard<std::mutex> lock(novelty_mutex_);
      published_track_ids_.clear();
    }
    new_detection_count_.store(0);

    // Reset tracker state for fresh session
    detector_->Reset();

    // Enable publisher
    detection_pub_.Enable();

    // Create 1 Hz processing timer
    auto node = ros_.get_node();
    timer_ = node->create_wall_timer(
        std::chrono::milliseconds(1000 / kFrequencyHz),
        std::bind(&BeetlePredatorController::TimerCallback, this));

    enabled_ = true;
    LOGI("BeetlePredator: Enabled (label_mask=0x%02x)", label_mask_.load());
  }

  void BeetlePredatorController::Disable()
  {
    if (!enabled_)
      return;

    enabled_ = false;

    // Destroy timer
    if (timer_)
    {
      timer_->cancel();
      timer_.reset();
    }

    // Disable publisher
    detection_pub_.Disable();

    // Disable the rear camera (we auto-enabled it on start)
    if (rear_camera_ && rear_camera_->IsEnabled())
    {
      LOGI("BeetlePredator: Disabling rear camera");
      rear_camera_->DisableCamera();
    }

    // Clear debug frames
    {
      std::lock_guard<std::mutex> lock(debug_frames_mutex_);
      debug_frames_jpeg_.clear();
    }

    LOGI("BeetlePredator: Disabled (published %d new detections)",
         new_detection_count_.load());
  }

  void BeetlePredatorController::TimerCallback()
  {
    if (!enabled_ || processing_.load())
      return;
    processing_.store(true);
    ProcessFrame();
    processing_.store(false);
  }

  void BeetlePredatorController::ProcessFrame()
  {
    auto t_start = std::chrono::steady_clock::now();

    // 1. Get latest camera frame (RGBA)
    std::vector<uint8_t> rgba_data;
    int width = 0, height = 0;
    if (!rear_camera_->GetLastFrame(rgba_data, width, height))
    {
      return; // No frame available yet
    }

    if (rgba_data.empty() || width <= 0 || height <= 0)
    {
      return;
    }

    auto t_acquire = std::chrono::steady_clock::now();

    // 2. Convert RGBA to BGR for NCNN pipeline
    size_t pixel_count = static_cast<size_t>(width) * height;
    std::vector<uint8_t> bgr_data(pixel_count * 3);
    for (size_t i = 0; i < pixel_count; i++)
    {
      bgr_data[i * 3 + 0] = rgba_data[i * 4 + 2]; // B <- A[2] (from RGBA R=0,G=1,B=2,A=3)
      bgr_data[i * 3 + 1] = rgba_data[i * 4 + 1]; // G
      bgr_data[i * 3 + 2] = rgba_data[i * 4 + 0]; // R
    }

    auto t_convert = std::chrono::steady_clock::now();

    // 3. Run ML pipeline (2D only, no depth)
    perception::PerceptionResult result = detector_->ProcessFrame(
        bgr_data.data(), width, height,
        nullptr, 0, 0, // no depth
        0.5f, 0.45f,   // confidence and IoU thresholds
        true);         // enable Deep SORT tracking

    auto t_inference = std::chrono::steady_clock::now();

    // 4. Get current GPS location
    sensor_msgs::msg::NavSatFix gps_fix;
    bool has_gps = gps_provider_ ? gps_provider_->GetLastLocation(gps_fix) : false;

    // 5. Filter and publish new detections
    uint8_t mask = label_mask_.load();

    for (const auto &track : result.tracks)
    {
      // Only publish confirmed tracks (n_init consecutive hits)
      if (!track.is_confirmed)
        continue;

      // Check label filter
      if (track.class_id < 0 || track.class_id > 2)
        continue;
      if (!(mask & (1 << track.class_id)))
        continue;

      // Check novelty (only publish new tracks)
      {
        std::lock_guard<std::mutex> lock(novelty_mutex_);
        if (published_track_ids_.count(track.track_id) > 0)
          continue;
        published_track_ids_.insert(track.track_id);
      }

      // Build and publish BeetleDetection message
      vermin_collector_ros_msgs::msg::BeetleDetection msg;

      // Header
      auto now = ros_.get_node()->now();
      msg.header.stamp = now;
      msg.header.frame_id = "beetle_predator";

      // GPS location
      if (has_gps)
      {
        msg.latitude = gps_fix.latitude;
        msg.longitude = gps_fix.longitude;
        msg.altitude = gps_fix.altitude;
        msg.horizontal_accuracy = static_cast<float>(
            std::sqrt(gps_fix.position_covariance[0]));
      }
      else
      {
        msg.latitude = 0.0;
        msg.longitude = 0.0;
        msg.altitude = 0.0;
        msg.horizontal_accuracy = -1.0f; // Indicates GPS unavailable
      }

      // Detection info
      msg.label = kClassNames[track.class_id];
      msg.class_id = track.class_id;
      msg.confidence = track.confidence;
      msg.track_id = track.track_id;

      // Bounding box (x1,y1,x2,y2 -> x,y,w,h)
      msg.bbox_x = track.bbox[0];
      msg.bbox_y = track.bbox[1];
      msg.bbox_width = track.bbox[2] - track.bbox[0];
      msg.bbox_height = track.bbox[3] - track.bbox[1];

      // Frame dimensions
      msg.frame_width = width;
      msg.frame_height = height;

      detection_pub_.Publish(msg);
      new_detection_count_.fetch_add(1);

      LOGI("BeetlePredator: Published new %s (track=%d, conf=%.2f, gps=%s)",
           msg.label.c_str(), track.track_id, track.confidence,
           has_gps ? "yes" : "no");
    }

    auto t_publish = std::chrono::steady_clock::now();

    // 6. Store debug frame if visualization enabled
    if (visualization_enabled_.load() && !result.annotated_rgb_bgr.empty())
    {
      // Encode BGR to JPEG using TurboJPEG
      tjhandle compressor = tjInitCompress();
      if (compressor)
      {
        unsigned char *jpeg_buf = nullptr;
        unsigned long jpeg_size = 0;

        int tj_result = tjCompress2(
            compressor,
            const_cast<uint8_t *>(result.annotated_rgb_bgr.data()),
            result.rgb_width,
            0, // pitch
            result.rgb_height,
            TJPF_BGR,
            &jpeg_buf,
            &jpeg_size,
            TJSAMP_420,
            85,
            TJFLAG_FASTDCT);

        if (tj_result == 0 && jpeg_buf && jpeg_size > 0)
        {
          std::vector<uint8_t> jpeg(jpeg_buf, jpeg_buf + jpeg_size);
          {
            std::lock_guard<std::mutex> lock(debug_frames_mutex_);
            debug_frames_jpeg_["beetle_predator_rgb"] = std::move(jpeg);
          }
          PostDebugFrameUpdate("beetle_predator_rgb");
        }

        if (jpeg_buf)
          tjFree(jpeg_buf);
        tjDestroy(compressor);
      }
    }

    auto t_end = std::chrono::steady_clock::now();

    auto ms = [](auto a, auto b)
    {
      return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    LOGD("BeetlePredator: Frame timing: acquire=%lldms rgba2bgr=%lldms "
         "inference=%lldms publish=%lldms jpeg=%lldms total=%lldms",
         ms(t_start, t_acquire), ms(t_acquire, t_convert),
         ms(t_convert, t_inference), ms(t_inference, t_publish),
         ms(t_publish, t_end), ms(t_start, t_end));
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
