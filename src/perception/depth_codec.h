#pragma once

#include <memory>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace ros2_android {

// Decompress a sensor_msgs/CompressedImage in compressedDepth format to a
// sensor_msgs/Image with encoding "32FC1" (float meters).
//
// The compressedDepth format from compressed_depth_image_transport prepends a
// 12-byte ConfigHeader { int32 format; float depth_quantization; float depth_max; }
// before a PNG-encoded 16UC1 depth image. Pixel value 0 maps to NaN.
// Formula: depth_meters = (pixel != 0) ? pixel / depth_quantization : NaN
//
// Returns nullptr on parse/decompress failure.
sensor_msgs::msg::Image::UniquePtr DecompressDepth(
    const sensor_msgs::msg::CompressedImage& msg);

}  // namespace ros2_android
