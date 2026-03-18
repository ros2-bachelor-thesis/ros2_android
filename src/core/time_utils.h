#pragma once

#include <cstdint>

namespace ros2_android {
namespace time_utils {

/**
 * Get the current offset between CLOCK_REALTIME (Unix epoch time) and
 * CLOCK_BOOTTIME (time since device boot).
 *
 * This is used to convert Android hardware sensor timestamps (which use
 * CLOCK_BOOTTIME) to ROS timestamps (which use Unix epoch time).
 *
 * @return Offset in nanoseconds, or 0 if clock_gettime fails
 */
int64_t GetBootTimeOffsetNs();

}  // namespace time_utils
}  // namespace ros2_android
