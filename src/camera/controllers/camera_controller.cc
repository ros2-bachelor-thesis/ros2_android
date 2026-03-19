#include "camera/controllers/camera_controller.h"

#include <libyuv.h>
#include <turbojpeg.h>
#include <sstream>

#include "core/camera_frame_callback_queue.h"
#include "core/log.h"
#include "core/notification_queue.h"

using ros2_android::CameraController;
using ros2_android::CameraDevice;
using ros2_android::NotificationSeverity;
using ros2_android::PostNotification;
using sensor_msgs::msg::CameraInfo;
using sensor_msgs::msg::CompressedImage;
using sensor_msgs::msg::Image;

CameraController::CameraController(CameraManager *camera_manager,
                                   const CameraDescriptor &camera_descriptor,
                                   RosInterface &ros)
    : camera_manager_(camera_manager),
      camera_descriptor_(camera_descriptor),
      SensorDataProvider(camera_descriptor.GetName()),
      ros_(ros),
      info_pub_(ros),
      image_pub_(ros),
      compressed_image_pub_(ros)
{
  // Prepend device_id to topic prefix
  std::string device_prefix = "/" + ros.GetDeviceId() + "/";
  std::string info_topic = device_prefix + camera_descriptor_.topic_prefix + "camera_info";
  std::string image_topic = device_prefix + camera_descriptor_.topic_prefix + "image_color";
  std::string compressed_image_topic = device_prefix + camera_descriptor_.topic_prefix + "image_color/compressed";

  info_pub_.SetTopic(info_topic.c_str());
  image_pub_.SetTopic(image_topic.c_str());
  compressed_image_pub_.SetTopic(compressed_image_topic.c_str());

  // Use BEST_EFFORT QoS for all topics (standard for camera streaming)
  // KeepLast(1): Only keep most recent message, drop old ones
  // BestEffort: Don't wait for delivery confirmation, drop if congested
  // Volatile: Don't store historical data for late joiners
  auto qos = rclcpp::QoS(rclcpp::KeepLast(1))
                 .best_effort()
                 .durability_volatile();
  info_pub_.SetQos(qos);
  image_pub_.SetQos(qos);
  compressed_image_pub_.SetQos(qos);
}

CameraController::~CameraController() {}

void CameraController::EnableCamera()
{
  device_ = camera_manager_->OpenCamera(camera_descriptor_, ros_.GetDeviceId());
  if (!device_)
  {
    LOGW("Failed to enable camera %s - could not open device (already in use?)",
         camera_descriptor_.display_name.c_str());
    PostNotification(
        NotificationSeverity::WARNING,
        "Failed to enable " + camera_descriptor_.display_name +
            " - could not open device (already in use?)");
    return;
  }
  info_pub_.Enable();
  image_pub_.Enable();
  compressed_image_pub_.Enable();
  device_->SetListener(
      std::bind(&CameraController::OnImage, this, std::placeholders::_1));
}

void CameraController::DisableCamera()
{
  info_pub_.Disable();
  image_pub_.Disable();
  compressed_image_pub_.Disable();
  device_.reset();
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    last_frame_.clear();
    last_frame_width_ = 0;
    last_frame_height_ = 0;
  }
}

std::string CameraController::PrettyName() const
{
  std::string name{camera_descriptor_.display_name};
  if (!device_)
  {
    name += " [disabled]";
  }
  return name;
}

std::string CameraController::GetLastMeasurementJson()
{
  std::ostringstream ss;
  ss << "{\"enabled\":" << (IsEnabled() ? "true" : "false");
  if (device_)
  {
    auto [width, height] = device_->Resolution();
    ss << ",\"resolution\":{\"width\":" << width << ",\"height\":" << height
       << "}";
  }
  ss << "}";
  return ss.str();
}

bool CameraController::GetLastMeasurement(jni::SensorReadingData &out_data)
{
  // Cameras don't provide sensor readings, only images
  // Return false to indicate no sensor data available
  return false;
}

void CameraController::OnImage(
    const std::pair<CameraInfo::UniquePtr, Image::UniquePtr> &info_image)
{
  // Publish camera_info (shared by both raw and compressed topics)
  if (info_pub_.Enabled())
  {
    size_t info_subscribers = info_pub_.GetSubscriberCount();
    if (info_subscribers > 0)
    {
      info_pub_.Publish(*info_image.first.get());
    }
  }

  // Publish raw image if there are subscribers
  if (image_pub_.Enabled())
  {
    size_t image_subscribers = image_pub_.GetSubscriberCount();
    if (image_subscribers > 0)
    {
      image_pub_.Publish(*info_image.second.get());
    }
  }

  // Publish compressed image if there are subscribers
  if (compressed_image_pub_.Enabled())
  {
    size_t compressed_image_subscribers = compressed_image_pub_.GetSubscriberCount();
    if (compressed_image_subscribers > 0)
    {
      const auto &bgr_data = info_image.second->data;
      int width = info_image.second->width;
      int height = info_image.second->height;

      // Convert BGR to RGB for JPEG encoding
      std::vector<uint8_t> rgb_data(width * height * 3);
      for (size_t i = 0; i < bgr_data.size(); i += 3)
      {
        rgb_data[i] = bgr_data[i + 2];     // R
        rgb_data[i + 1] = bgr_data[i + 1]; // G
        rgb_data[i + 2] = bgr_data[i];     // B
      }

      // Compress to JPEG using TurboJPEG
      tjhandle compressor = tjInitCompress();
      if (compressor)
      {
        unsigned char *jpeg_buf = nullptr;
        unsigned long jpeg_size = 0;

        int tj_result = tjCompress2(
            compressor,
            rgb_data.data(),
            width,
            width * 3, // pitch (bytes per row)
            height,
            TJPF_RGB,
            &jpeg_buf,
            &jpeg_size,
            TJSAMP_420, // 4:2:0 chroma subsampling
            85,         // quality (0-100)
            TJFLAG_FASTDCT);

        if (tj_result == 0)
        {
          auto compressed_msg = std::make_unique<CompressedImage>();
          compressed_msg->header = info_image.second->header;
          compressed_msg->format = "jpeg";
          compressed_msg->data.assign(jpeg_buf, jpeg_buf + jpeg_size);
          compressed_image_pub_.Publish(*compressed_msg);
        }

        if (jpeg_buf)
          tjFree(jpeg_buf);
        tjDestroy(compressor);
      }
    }
  }

  // Convert BGR8 to ARGB for UI preview
  // The BGR image from camera_device is already rotated for ROS2/rqt_image_view
  // For Android UI, we need to rotate it back to match device portrait orientation
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    const auto &bgr_data = info_image.second->data;
    int width = info_image.second->width;
    int height = info_image.second->height;

    // Convert BGR24 to ARGB first
    std::vector<uint8_t> argb_buffer(width * height * 4);
    libyuv::RAWToARGB(
        bgr_data.data(), width * 3,
        argb_buffer.data(), width * 4,
        width, height);

    // Determine inverse rotation to undo sensor_orientation rotation
    // This converts from ROS2-oriented image back to device portrait orientation
    libyuv::RotationMode inverse_rotation;
    switch (camera_descriptor_.sensor_orientation)
    {
    case 0:
      inverse_rotation = libyuv::kRotate0;
      break;
    case 90:
      inverse_rotation = libyuv::kRotate270; // undo 90° CW with 270° CW (= 90° CCW)
      break;
    case 180:
      inverse_rotation = libyuv::kRotate180; // undo 180° with 180°
      break;
    case 270:
      inverse_rotation = libyuv::kRotate90; // undo 270° CW with 90° CW (= 270° CCW)
      break;
    default:
      inverse_rotation = libyuv::kRotate0;
      break;
    }

    // Calculate dimensions after inverse rotation
    bool swaps_dimensions = (inverse_rotation == libyuv::kRotate90 || inverse_rotation == libyuv::kRotate270);
    int rotated_width = swaps_dimensions ? height : width;
    int rotated_height = swaps_dimensions ? width : height;

    last_frame_.resize(rotated_width * rotated_height * 4);
    last_frame_width_ = rotated_width;
    last_frame_height_ = rotated_height;

    libyuv::ARGBRotate(
        argb_buffer.data(), width * 4,
        last_frame_.data(), rotated_width * 4,
        width, height,
        inverse_rotation);
  }

  // Trigger callback to notify UI of new camera frame (throttled to 10 Hz)
  ros2_android::PostCameraFrameUpdate(std::string(static_cast<const SensorDataProvider *>(this)->UniqueId()));
}

bool CameraController::GetLastFrame(std::vector<uint8_t> &out_data,
                                    int &out_width, int &out_height)
{
  std::lock_guard<std::mutex> lock(frame_mutex_);
  if (last_frame_.empty())
    return false;
  out_data = last_frame_; // RGBA data
  out_width = last_frame_width_;
  out_height = last_frame_height_;
  return true;
}
