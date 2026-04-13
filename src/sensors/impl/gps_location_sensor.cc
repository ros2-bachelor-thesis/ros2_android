#include "sensors/impl/gps_location_sensor.h"

#include <cmath>
#include <cstdint>

#include "core/log.h"
#include "core/time_utils.h"

namespace ros2_android
{

  void GpsLocationProvider::OnLocationUpdate(double latitude, double longitude,
                                             double altitude, float accuracy,
                                             float altitude_accuracy,
                                             int64_t timestamp_ns)
  {
    sensor_msgs::msg::NavSatFix msg;

    // Convert Android hardware timestamp (nanoseconds since boot) to ROS epoch time
    int64_t offset_ns = time_utils::GetBootTimeOffsetNs();
    int64_t ros_epoch_timestamp_ns = timestamp_ns + offset_ns;

    msg.header.stamp.sec = static_cast<int32_t>(ros_epoch_timestamp_ns / 1000000000LL);
    msg.header.stamp.nanosec = static_cast<uint32_t>(ros_epoch_timestamp_ns % 1000000000LL);
    msg.header.frame_id = "gps";

    // Set GPS fix status
    msg.status.status = sensor_msgs::msg::NavSatStatus::STATUS_FIX;
    msg.status.service = sensor_msgs::msg::NavSatStatus::SERVICE_GPS;

    // Set position
    msg.latitude = latitude;
    msg.longitude = longitude;
    msg.altitude = altitude;

    // Set covariance (ENU - East, North, Up)
    // Android provides horizontal and vertical accuracy in meters (1-sigma)
    // Covariance = variance = sigma^2
    double horizontal_variance = accuracy * accuracy;
    double vertical_variance = altitude_accuracy * altitude_accuracy;

    // Initialize covariance matrix to zero
    for (int i = 0; i < 9; i++)
    {
      msg.position_covariance[i] = 0.0;
    }

    // Diagonal: East, North, Up variances
    msg.position_covariance[0] = horizontal_variance; // East
    msg.position_covariance[4] = horizontal_variance; // North
    msg.position_covariance[8] = vertical_variance;   // Up

    msg.position_covariance_type =
        sensor_msgs::msg::NavSatFix::COVARIANCE_TYPE_DIAGONAL_KNOWN;

    LOGI("GPS Update: lat=%.6f, lon=%.6f, alt=%.2f, acc=%.2fm", latitude,
         longitude, altitude, accuracy);

    // Cache for GetLastLocation() consumers (always cache, even without callback)
    {
      std::lock_guard<std::mutex> lock(location_mutex_);
      last_location_ = msg;
      has_location_ = true;
    }

    if (location_callback_)
    {
      location_callback_(msg);
    }
  }

  bool GpsLocationProvider::GetLastLocation(sensor_msgs::msg::NavSatFix& out)
  {
    std::lock_guard<std::mutex> lock(location_mutex_);
    if (!has_location_)
    {
      return false;
    }
    out = last_location_;
    return true;
  }

} // namespace ros2_android