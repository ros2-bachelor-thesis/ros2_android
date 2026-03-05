#pragma once

#include <sensor_msgs/msg/magnetic_field.hpp>
#include <mutex>

#include "log.h"
#include "ros_interface.h"
#include "sensor_data_provider.h"
#include "sensors/magnetometer_sensor.h"

namespace sensors_for_ros {

class MagnetometerSensorController : public SensorDataProvider {
 public:
  MagnetometerSensorController(MagnetometerSensor* sensor, RosInterface& ros);
  virtual ~MagnetometerSensorController() = default;

  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;

  const char* SensorName() const override { return sensor_->Descriptor().name; }
  const char* SensorVendor() const override { return sensor_->Descriptor().vendor; }
  const char* TopicName() const override { return publisher_.Topic(); }
  const char* TopicType() const override { return publisher_.Type(); }

 protected:
  void OnSensorReading(const sensor_msgs::msg::MagneticField& msg);

 private:
  std::mutex mutex_;
  sensor_msgs::msg::MagneticField last_msg_;
  MagnetometerSensor* sensor_;
  Publisher<sensor_msgs::msg::MagneticField> publisher_;
};

}  // namespace sensors_for_ros
