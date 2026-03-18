#include "sensors/impl/magnetometer_sensor.h"

#include <cstdint>
#include "core/log.h"
#include "core/time_utils.h"

using ros2_android::MagnetometerSensor;

void MagnetometerSensor::OnEvent(const ASensorEvent& event) {
  if (ASENSOR_TYPE_MAGNETIC_FIELD != event.type) {
    LOGW("Event type was unexpected: %d", event.type);
    return;
  }

  sensor_msgs::msg::MagneticField msg;

  // Convert Android hardware timestamp (nanoseconds since boot) to ROS epoch time
  int64_t offset_ns = ros2_android::time_utils::GetBootTimeOffsetNs();
  int64_t ros_epoch_timestamp_ns = event.timestamp + offset_ns;

  msg.header.stamp.sec = static_cast<int32_t>(ros_epoch_timestamp_ns / 1000000000LL);
  msg.header.stamp.nanosec = static_cast<uint32_t>(ros_epoch_timestamp_ns % 1000000000LL);
  msg.header.frame_id = "magnetometer";

  msg.magnetic_field.x = event.magnetic.x / kMicroTeslaPerTesla;
  msg.magnetic_field.y = event.magnetic.y / kMicroTeslaPerTesla;
  msg.magnetic_field.z = event.magnetic.z / kMicroTeslaPerTesla;
  Emit(msg);
}
