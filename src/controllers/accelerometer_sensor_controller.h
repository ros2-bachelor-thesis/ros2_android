#pragma once

#include <geometry_msgs/msg/accel_stamped.hpp>
#include <mutex>

#include "log.h"
#include "ros_interface.h"
#include "sensor_data_provider.h"
#include "sensors/accelerometer_sensor.h"

namespace ros2_android {

class AccelerometerSensorController : public SensorDataProvider {
 public:
  AccelerometerSensorController(AccelerometerSensor* sensor, RosInterface& ros);
  virtual ~AccelerometerSensorController() = default;

  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;

  const char* SensorName() const override { return sensor_->Descriptor().name; }
  const char* SensorVendor() const override { return sensor_->Descriptor().vendor; }
  const char* TopicName() const override { return publisher_.Topic(); }
  const char* TopicType() const override { return publisher_.Type(); }

  void Enable() override { publisher_.Enable(); }
  void Disable() override { publisher_.Disable(); }
  bool IsEnabled() const override { return publisher_.Enabled(); }

 protected:
  void OnSensorReading(const geometry_msgs::msg::AccelStamped& msg);

 private:
  std::mutex mutex_;
  geometry_msgs::msg::AccelStamped last_msg_;
  AccelerometerSensor* sensor_;
  Publisher<geometry_msgs::msg::AccelStamped> publisher_;
};

}  // namespace ros2_android
