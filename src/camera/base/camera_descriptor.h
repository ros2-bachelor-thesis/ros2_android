#pragma once

#include <camera/NdkCameraMetadata.h>

#include <string>

namespace ros2_android
{
  struct CameraDescriptor
  {
    std::string GetName() const;

    // An id identifying the camera
    std::string id;

    // Human-readable display name set by CameraManager after discovery
    // (e.g. "Front Camera" or "Rear Camera 2")
    std::string display_name;

    // Topic path prefix set by CameraManager after discovery
    // (e.g. "camera/front/" or "camera/rear_2/")
    std::string topic_prefix;

    // Which way the lens if facing (back, external or front).
    acamera_metadata_enum_acamera_lens_facing lens_facing;

    // True if this camera is a logical multi-camera device (fuses multiple
    // physical sensors).  Used to prefer it over standalone physical cameras
    // that share the same facing direction.
    bool is_logical_multi_camera = false;

    // Clockwise angle (0, 90, 180, 270) the sensor image must be rotated to
    // match the device's natural (portrait) orientation.
    int sensor_orientation = 0;

    // TODO intrinsics, supported resolutions, supported frame rates,
    // distortion parameters, etc.
  };
} // namespace ros2_android
