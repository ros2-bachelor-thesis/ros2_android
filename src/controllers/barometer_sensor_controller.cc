#include "controllers/barometer_sensor_controller.h"

#include <sstream>

namespace ros2_android {

BarometerSensorController::BarometerSensorController(BarometerSensor* sensor,
                                                     RosInterface& ros)
    : sensor_(sensor),
      publisher_(ros),
      SensorDataProvider(std::string(sensor->Descriptor().name) +
                         sensor->Descriptor().vendor) {
  sensor->SetListener(std::bind(&BarometerSensorController::OnSensorReading,
                                this, std::placeholders::_1));
  publisher_.SetTopic("barometer");
}

void BarometerSensorController::OnSensorReading(
    const sensor_msgs::msg::FluidPressure& msg) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = msg;
  }
  publisher_.Publish(msg);
}

std::string BarometerSensorController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;
  ss << "{\"values\":[" << last_msg_.fluid_pressure
     << "],\"unit\":\"Pa\"}";
  return ss.str();
}

std::string BarometerSensorController::PrettyName() const {
  return "Barometer Sensor";
}

}  // namespace ros2_android
