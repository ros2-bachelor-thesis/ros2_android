#pragma once

#include <sensor_msgs/msg/illuminance.hpp>
#include <mutex>

#include "log.h"
#include "ros_interface.h"
#include "sensor_data_provider.h"
#include "sensors/illuminance_sensor.h"

namespace sensors_for_ros {

class IlluminanceSensorController : public SensorDataProvider {
 public:
  IlluminanceSensorController(IlluminanceSensor* sensor, RosInterface& ros);
  virtual ~IlluminanceSensorController() = default;

  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;

  const char* SensorName() const override { return sensor_->Descriptor().name; }
  const char* SensorVendor() const override { return sensor_->Descriptor().vendor; }
  const char* TopicName() const override { return publisher_.Topic(); }
  const char* TopicType() const override { return publisher_.Type(); }

 protected:
  void OnIlluminanceChanged(const sensor_msgs::msg::Illuminance& msg);

 private:
  std::mutex mutex_;
  sensor_msgs::msg::Illuminance last_msg_;
  IlluminanceSensor* sensor_;
  Publisher<sensor_msgs::msg::Illuminance> publisher_;
};

}  // namespace sensors_for_ros
