#include "controllers/magnetometer_sensor_controller.h"

#include <sstream>

namespace ros2_android {

MagnetometerSensorController::MagnetometerSensorController(
    MagnetometerSensor* sensor, RosInterface& ros)
    : sensor_(sensor),
      publisher_(ros),
      SensorDataProvider(std::string(sensor->Descriptor().name) +
                         sensor->Descriptor().vendor) {
  sensor->SetListener(std::bind(&MagnetometerSensorController::OnSensorReading,
                                this, std::placeholders::_1));
  publisher_.SetTopic("magnetometer");
}

void MagnetometerSensorController::OnSensorReading(
    const sensor_msgs::msg::MagneticField& msg) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = msg;
  }
  publisher_.Publish(msg);
}

std::string MagnetometerSensorController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;
  ss << "{\"values\":["
     << last_msg_.magnetic_field.x * kMicroTeslaPerTesla << ","
     << last_msg_.magnetic_field.y * kMicroTeslaPerTesla << ","
     << last_msg_.magnetic_field.z * kMicroTeslaPerTesla
     << "],\"unit\":\"uT\"}";
  return ss.str();
}

std::string MagnetometerSensorController::PrettyName() const {
  return "Magnetometer Sensor";
}

}  // namespace ros2_android
