#pragma once

#include <camera/NdkCameraManager.h>

#include <vector>

#include "camera/base/camera_descriptor.h"
#include "camera/base/camera_device.h"

namespace ros2_android {
class CameraManager {
 public:
  CameraManager();
  ~CameraManager();

  bool HasCameras() const { return !cameras_.empty(); }

  const std::vector<CameraDescriptor>& GetCameras() { return cameras_; }

  std::unique_ptr<CameraDevice> OpenCamera(const CameraDescriptor& desc, const std::string& device_id) const;

 private:
  void DiscoverCameras();

  ACameraManager* native_manager_ = nullptr;
  std::vector<CameraDescriptor> cameras_;
};
}  // namespace ros2_android
