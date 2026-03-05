#pragma once

#include <memory>

#include "camera_device.h"
#include "camera_manager.h"
#include "ros_interface.h"
#include "sensor_data_provider.h"

namespace sensors_for_ros {

class CameraController : public SensorDataProvider {
 public:
  CameraController(CameraManager* camera_manager,
                   const CameraDescriptor& camera_descriptor,
                   RosInterface& ros);
  virtual ~CameraController();

  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;

  void EnableCamera();
  void DisableCamera();

  bool IsEnabled() const { return image_pub_.Enabled(); }
  const char* ImageTopicName() const { return image_pub_.Topic(); }
  const char* ImageTopicType() const { return image_pub_.Type(); }
  const char* InfoTopicName() const { return info_pub_.Topic(); }
  const char* InfoTopicType() const { return info_pub_.Type(); }
  std::string GetCameraName() const { return camera_descriptor_.GetName(); }

  std::tuple<int, int> GetResolution() const {
    if (device_) {
      return device_->Resolution();
    }
    return {0, 0};
  }

 protected:
  void OnImage(
      const std::pair<CameraInfo::UniquePtr, Image::UniquePtr>& info_image);

 private:
  CameraManager* camera_manager_;
  const CameraDescriptor camera_descriptor_;
  std::unique_ptr<CameraDevice> device_;
  Publisher<sensor_msgs::msg::CameraInfo> info_pub_;
  Publisher<sensor_msgs::msg::Image> image_pub_;
};

}  // namespace sensors_for_ros
