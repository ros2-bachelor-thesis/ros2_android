#pragma once

#include <sensor_msgs/msg/fluid_pressure.hpp>
#include <mutex>

#include "log.h"
#include "ros_interface.h"
#include "sensor_data_provider.h"
#include "sensors/barometer_sensor.h"

namespace sensors_for_ros {

class BarometerSensorController : public SensorDataProvider {
 public:
  BarometerSensorController(BarometerSensor* sensor, RosInterface& ros);
  virtual ~BarometerSensorController() = default;

  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;

  const char* SensorName() const override { return sensor_->Descriptor().name; }
  const char* SensorVendor() const override { return sensor_->Descriptor().vendor; }
  const char* TopicName() const override { return publisher_.Topic(); }
  const char* TopicType() const override { return publisher_.Type(); }

 protected:
  void OnSensorReading(const sensor_msgs::msg::FluidPressure& msg);

 private:
  std::mutex mutex_;
  sensor_msgs::msg::FluidPressure last_msg_;
  BarometerSensor* sensor_;
  Publisher<sensor_msgs::msg::FluidPressure> publisher_;
};

}  // namespace sensors_for_ros
