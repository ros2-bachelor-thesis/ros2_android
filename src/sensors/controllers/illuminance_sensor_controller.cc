#include "sensors/controllers/illuminance_sensor_controller.h"

#include <sstream>

#include "core/sensor_data_callback_queue.h"

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

  // Trigger callback to notify UI of new sensor data (throttled to 10 Hz)
  ros2_android::PostSensorDataUpdate(std::string(UniqueId()));
}

std::string IlluminanceSensorController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;
  ss << "{\"values\":[" << last_msg_.illuminance
     << "],\"unit\":\"lx\"}";
  return ss.str();
}

bool IlluminanceSensorController::GetLastMeasurement(jni::SensorReadingData& out_data) {
  std::lock_guard<std::mutex> lock(mutex_);
  out_data.values = {last_msg_.illuminance};
  out_data.unit = "lx";
  return true;
}

std::string IlluminanceSensorController::PrettyName() const {
  return "Light Sensor";
}

}  // namespace ros2_android
