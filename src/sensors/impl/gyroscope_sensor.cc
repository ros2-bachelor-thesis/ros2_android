#include "sensors/impl/gyroscope_sensor.h"

#include <cstdint>
#include "core/log.h"
#include "core/time_utils.h"

using ros2_android::GyroscopeSensor;

void GyroscopeSensor::OnEvent(const ASensorEvent& event) {
  if (ASENSOR_TYPE_GYROSCOPE != event.type) {
    LOGW("Event type was unexpected: %d", event.type);
    return;
  }

  geometry_msgs::msg::TwistStamped msg;

  // Convert Android hardware timestamp (nanoseconds since boot) to ROS epoch time
  int64_t offset_ns = ros2_android::time_utils::GetBootTimeOffsetNs();
  int64_t ros_epoch_timestamp_ns = event.timestamp + offset_ns;

  msg.header.stamp.sec = static_cast<int32_t>(ros_epoch_timestamp_ns / 1000000000LL);
  msg.header.stamp.nanosec = static_cast<uint32_t>(ros_epoch_timestamp_ns % 1000000000LL);
  msg.header.frame_id = "gyroscope";

  msg.twist.angular.x = event.vector.x;
  msg.twist.angular.y = event.vector.y;
  msg.twist.angular.z = event.vector.z;
  Emit(msg);
}
