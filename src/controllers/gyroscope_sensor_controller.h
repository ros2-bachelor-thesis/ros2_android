#pragma once

#include <geometry_msgs/msg/twist_stamped.hpp>
#include <mutex>

#include "log.h"
#include "ros_interface.h"
#include "sensor_data_provider.h"
#include "sensors/gyroscope_sensor.h"

namespace ros2_android {

class GyroscopeSensorController : public SensorDataProvider {
 public:
  GyroscopeSensorController(GyroscopeSensor* sensor, RosInterface& ros);
  virtual ~GyroscopeSensorController() = default;

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
  void OnGyroReading(const geometry_msgs::msg::TwistStamped& msg);

 private:
  std::mutex mutex_;
  geometry_msgs::msg::TwistStamped last_msg_;
  GyroscopeSensor* sensor_;
  Publisher<geometry_msgs::msg::TwistStamped> publisher_;
};

}  // namespace ros2_android
