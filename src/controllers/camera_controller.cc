#include "camera_controller.h"

#include <sstream>

#include "log.h"

using sensors_for_ros::CameraController;
using sensors_for_ros::CameraDevice;

CameraController::CameraController(CameraManager* camera_manager,
                                   const CameraDescriptor& camera_descriptor,
                                   RosInterface& ros)
    : camera_manager_(camera_manager),
      camera_descriptor_(camera_descriptor),
      SensorDataProvider(camera_descriptor.GetName()),
      info_pub_(ros),
      image_pub_(ros) {
  std::stringstream base_topic;
  base_topic << "camera/id_" << camera_descriptor_.id << "/";

  std::string info_topic = base_topic.str() + "camera_info";
  std::string image_topic = base_topic.str() + "image_color";

  info_pub_.SetTopic(info_topic.c_str());
  image_pub_.SetTopic(image_topic.c_str());
  image_pub_.SetQos(rclcpp::QoS(1).best_effort());
}

CameraController::~CameraController() {}

void CameraController::EnableCamera() {
  image_pub_.Enable();
  info_pub_.Enable();
  device_ = camera_manager_->OpenCamera(camera_descriptor_);
  device_->SetListener(
      std::bind(&CameraController::OnImage, this, std::placeholders::_1));
}

void CameraController::DisableCamera() {
  image_pub_.Disable();
  info_pub_.Disable();
  device_.reset();
}

std::string CameraController::PrettyName() const {
  std::string name{camera_descriptor_.GetName()};
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

void CameraController::OnImage(
    const std::pair<CameraInfo::UniquePtr, Image::UniquePtr>& info_image) {
  LOGI("Controller has image?");
  info_pub_.Publish(*info_image.first.get());
  image_pub_.Publish(*info_image.second.get());
}
