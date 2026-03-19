#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <sensor_msgs/msg/compressed_image.hpp>

#include "camera/base/camera_device.h"
#include "camera/camera_manager.h"
#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"

namespace ros2_android {

class CameraController : public SensorDataProvider {
 public:
  CameraController(CameraManager* camera_manager,
                   const CameraDescriptor& camera_descriptor,
                   RosInterface& ros);
  virtual ~CameraController();

  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  void EnableCamera();
  void DisableCamera();

  void Enable() override { EnableCamera(); }
  void Disable() override { DisableCamera(); }
  bool IsEnabled() const override { return image_pub_.Enabled(); }
  const char* ImageTopicName() const { return image_pub_.Topic(); }
  const char* ImageTopicType() const { return image_pub_.Type(); }
  const char* CompressedImageTopicName() const { return compressed_image_pub_.Topic(); }
  const char* CompressedImageTopicType() const { return compressed_image_pub_.Type(); }
  const char* InfoTopicName() const { return info_pub_.Topic(); }
  const char* InfoTopicType() const { return info_pub_.Type(); }
  std::string GetCameraName() const { return camera_descriptor_.display_name; }
  bool IsFrontFacing() const {
    return camera_descriptor_.lens_facing == ACAMERA_LENS_FACING_FRONT;
  }
  int SensorOrientation() const { return camera_descriptor_.sensor_orientation; }

  std::tuple<int, int> GetResolution() const {
    if (device_) {
      return device_->Resolution();
    }
    return {0, 0};
  }

  bool GetLastFrame(std::vector<uint8_t>& out_data, int& out_width,
                    int& out_height);

 protected:
  void OnImage(
      const std::pair<CameraInfo::UniquePtr, Image::UniquePtr>& info_image);

 private:
  CameraManager* camera_manager_;
  const CameraDescriptor camera_descriptor_;
  RosInterface& ros_;
  std::unique_ptr<CameraDevice> device_;
  Publisher<sensor_msgs::msg::CameraInfo> info_pub_;
  Publisher<sensor_msgs::msg::Image> image_pub_;
  Publisher<sensor_msgs::msg::CompressedImage> compressed_image_pub_;

  std::mutex frame_mutex_;
  std::vector<uint8_t> last_frame_;  // Raw RGBA frame data
  int last_frame_width_ = 0;
  int last_frame_height_ = 0;
};

}  // namespace ros2_android
