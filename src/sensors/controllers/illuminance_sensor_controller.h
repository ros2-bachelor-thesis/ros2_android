#pragma once

#include <sensor_msgs/msg/illuminance.hpp>
#include <mutex>

#include "core/log.h"
#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"
#include "sensors/impl/illuminance_sensor.h"

namespace ros2_android {

class IlluminanceSensorController : public SensorDataProvider {
 public:
  IlluminanceSensorController(IlluminanceSensor* sensor, RosInterface& ros);
  virtual ~IlluminanceSensorController() = default;

  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  const char* SensorName() const override { return sensor_->Descriptor().name; }
  const char* SensorVendor() const override { return sensor_->Descriptor().vendor; }
  const char* TopicName() const override { return publisher_.Topic(); }
  const char* TopicType() const override { return publisher_.Type(); }

  void Enable() override { publisher_.Enable(); }
  void Disable() override { publisher_.Disable(); }
  bool IsEnabled() const override { return publisher_.Enabled(); }

 protected:
  void OnIlluminanceChanged(const sensor_msgs::msg::Illuminance& msg);

 private:
  std::mutex mutex_;
  sensor_msgs::msg::Illuminance last_msg_;
  IlluminanceSensor* sensor_;
  Publisher<sensor_msgs::msg::Illuminance> publisher_;
};

}  // namespace ros2_android
