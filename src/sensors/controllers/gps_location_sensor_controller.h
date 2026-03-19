#pragma once

#include <mutex>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include "core/log.h"
#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"
#include "sensors/impl/gps_location_sensor.h"

namespace ros2_android {

/**
 * @brief GPS location controller that publishes NavSatFix messages
 *
 * Receives GPS location data from GpsLocationProvider and publishes
 * sensor_msgs/msg/NavSatFix messages to ROS 2.
 */
class GpsController : public SensorDataProvider {
 public:
  GpsController(GpsLocationProvider* provider, RosInterface& ros);
  virtual ~GpsController() = default;

  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  const char* SensorName() const override { return "GPS Location"; }
  const char* SensorVendor() const override { return "FusedLocationProvider"; }
  const char* TopicName() const override { return publisher_.Topic(); }
  const char* TopicType() const override { return publisher_.Type(); }

  void Enable() override;
  void Disable() override;
  bool IsEnabled() const override { return publisher_.Enabled(); }

 protected:
  void OnLocationUpdate(const sensor_msgs::msg::NavSatFix& msg);

 private:
  std::mutex mutex_;
  sensor_msgs::msg::NavSatFix last_msg_;
  GpsLocationProvider* provider_;
  Publisher<sensor_msgs::msg::NavSatFix> publisher_;
  RosInterface& ros_;
};

}  // namespace ros2_android
