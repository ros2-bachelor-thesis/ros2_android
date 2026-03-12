#include "camera/controllers/camera_controller.h"

#include <sstream>

#include "core/log.h"
#include "core/notification_queue.h"

using ros2_android::CameraController;
using ros2_android::CameraDevice;
using ros2_android::NotificationSeverity;
using ros2_android::PostNotification;

CameraController::CameraController(CameraManager* camera_manager,
                                   const CameraDescriptor& camera_descriptor,
                                   RosInterface& ros)
    : camera_manager_(camera_manager),
      camera_descriptor_(camera_descriptor),
      SensorDataProvider(camera_descriptor.GetName()),
      info_pub_(ros),
      image_pub_(ros) {
  std::string info_topic = camera_descriptor_.topic_prefix + "camera_info";
  std::string image_topic = camera_descriptor_.topic_prefix + "image_color";

  info_pub_.SetTopic(info_topic.c_str());
  image_pub_.SetTopic(image_topic.c_str());
  image_pub_.SetQos(rclcpp::QoS(1).best_effort());
}

CameraController::~CameraController() {}

void CameraController::EnableCamera() {
  device_ = camera_manager_->OpenCamera(camera_descriptor_);
  if (!device_) {
    LOGW("Failed to enable camera %s - could not open device (already in use?)",
         camera_descriptor_.display_name.c_str());
    PostNotification(
        NotificationSeverity::WARNING,
        "Failed to enable " + camera_descriptor_.display_name +
            " - could not open device (already in use?)");
    return;
  }
  image_pub_.Enable();
  info_pub_.Enable();
  device_->SetListener(
      std::bind(&CameraController::OnImage, this, std::placeholders::_1));
}

void CameraController::DisableCamera() {
  image_pub_.Disable();
  info_pub_.Disable();
  device_.reset();
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    last_frame_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
  }
}

std::string CameraController::PrettyName() const {
  std::string name{camera_descriptor_.display_name};
  if (!device_) {
    name += " [disabled]";
  }
  return name;
}

std::string CameraController::GetLastMeasurementJson() {
  std::ostringstream ss;
  ss << "{\"enabled\":" << (IsEnabled() ? "true" : "false");
  if (device_) {
    auto [width, height] = device_->Resolution();
    ss << ",\"resolution\":{\"width\":" << width << ",\"height\":" << height
       << "}";
  }
  ss << "}";
  return ss.str();
}

bool CameraController::GetLastMeasurement(jni::SensorReadingData& out_data) {
  // Cameras don't provide sensor readings, only images
  // Return false to indicate no sensor data available
  return false;
}

void CameraController::OnImage(
    const std::pair<CameraInfo::UniquePtr, Image::UniquePtr>& info_image) {
  LOGI("Controller::OnImage - received image %dx%d",
       info_image.second->width, info_image.second->height);
  LOGI("Publisher enabled: info=%d image=%d",
       info_pub_.Enabled(), image_pub_.Enabled());
  info_pub_.Publish(*info_image.first.get());
  image_pub_.Publish(*info_image.second.get());
  LOGI("Published camera data");

  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    last_frame_ = info_image.second->data;
    frame_width_ = info_image.second->width;
    frame_height_ = info_image.second->height;
  }
}

bool CameraController::GetLastFrame(std::vector<uint8_t>& out_data,
                                    int& out_width, int& out_height) {
  std::lock_guard<std::mutex> lock(frame_mutex_);
  if (last_frame_.empty()) return false;
  out_data = last_frame_;
  out_width = frame_width_;
  out_height = frame_height_;
  return true;
}
