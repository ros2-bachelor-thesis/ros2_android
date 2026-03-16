#include "camera/base/camera_descriptor.h"

#include <sstream>

using ros2_android::CameraDescriptor;

std::string CameraDescriptor::GetName() const
{
  std::stringstream name;
  switch (lens_facing)
  {
  case ACAMERA_LENS_FACING_BACK:
    name << "Rear ";
    break;
  case ACAMERA_LENS_FACING_EXTERNAL:
    name << "External ";
    break;
  case ACAMERA_LENS_FACING_FRONT:
    name << "Front ";
    break;
  default:
    break;
  }

  name << "camera (" << id << ")";
  return name.str();
}
