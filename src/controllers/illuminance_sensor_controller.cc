#include "controllers/illuminance_sensor_controller.h"

#include <sstream>

namespace ros2_android {

IlluminanceSensorController::IlluminanceSensorController(
    IlluminanceSensor* sensor, RosInterface& ros)
    : sensor_(sensor),
      publisher_(ros),
      SensorDataProvider(std::string(sensor->Descriptor().name) +
                         sensor->Descriptor().vendor) {
  sensor->SetListener(
      std::bind(&IlluminanceSensorController::OnIlluminanceChanged, this,
                std::placeholders::_1));
  publisher_.SetTopic("illuminance");
}

void IlluminanceSensorController::OnIlluminanceChanged(
    const sensor_msgs::msg::Illuminance& msg) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = msg;
  }
  publisher_.Publish(msg);
}

std::string IlluminanceSensorController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;
  ss << "{\"values\":[" << last_msg_.illuminance
     << "],\"unit\":\"lx\"}";
  return ss.str();
}

std::string IlluminanceSensorController::PrettyName() const {
  return "Light Sensor";
}

}  // namespace ros2_android
