#pragma once

#include <functional>
#include <mutex>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

namespace ros2_android {

/**
 * @brief GPS location data provider
 *
 * Receives GPS location data from Android's FusedLocationProviderClient via JNI
 * and provides it to GPS controllers for ROS publishing.
 */
class GpsLocationProvider {
 public:
  using LocationCallback = std::function<void(const sensor_msgs::msg::NavSatFix&)>;

  GpsLocationProvider() = default;
  virtual ~GpsLocationProvider() = default;

  /**
   * @brief Set callback to receive GPS location updates
   */
  void SetLocationCallback(LocationCallback callback) {
    location_callback_ = callback;
  }

  /**
   * @brief Called from JNI when new location data arrives
   * @param latitude Latitude in degrees (positive = north, negative = south)
   * @param longitude Longitude in degrees (positive = east, negative = west)
   * @param altitude Altitude in meters above WGS84 ellipsoid
   * @param accuracy Horizontal accuracy in meters
   * @param altitude_accuracy Vertical accuracy in meters
   * @param timestamp_ns Android timestamp in nanoseconds
   */
  void OnLocationUpdate(double latitude, double longitude, double altitude,
                       float accuracy, float altitude_accuracy,
                       int64_t timestamp_ns);

  /**
   * @brief Get the most recent GPS location
   * @param out Filled with the latest NavSatFix if available
   * @return true if a location is available, false otherwise
   */
  bool GetLastLocation(sensor_msgs::msg::NavSatFix& out);

  /**
   * @brief Get unique identifier for this provider
   */
  const char* UniqueId() const { return "gps_location_provider"; }

 private:
  LocationCallback location_callback_;

  std::mutex location_mutex_;
  sensor_msgs::msg::NavSatFix last_location_;
  bool has_location_ = false;
};

}  // namespace ros2_android
