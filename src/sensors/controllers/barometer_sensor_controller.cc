#include "sensors/controllers/barometer_sensor_controller.h"

#include <sstream>

#include "core/sensor_data_callback_queue.h"

namespace ros2_android {

BarometerSensorController::BarometerSensorController(BarometerSensor* sensor,
                                                     RosInterface& ros)
    : sensor_(sensor),
      publisher_(ros),
      SensorDataProvider(std::string(sensor->Descriptor().name) +
                         sensor->Descriptor().vendor) {
  sensor->SetListener(std::bind(&BarometerSensorController::OnSensorReading,
                                this, std::placeholders::_1));
  publisher_.SetTopic("/sensors/barometer");
}

void BarometerSensorController::OnSensorReading(
    const sensor_msgs::msg::FluidPressure& msg) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = msg;
  }
  publisher_.Publish(msg);

  // Trigger callback to notify UI of new sensor data (throttled to 10 Hz)
  ros2_android::PostSensorDataUpdate(std::string(UniqueId()));
}

std::string BarometerSensorController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;
  ss << "{\"values\":[" << last_msg_.fluid_pressure
     << "],\"unit\":\"Pa\"}";
  return ss.str();
}

bool BarometerSensorController::GetLastMeasurement(jni::SensorReadingData& out_data) {
  std::lock_guard<std::mutex> lock(mutex_);
  out_data.values = {last_msg_.fluid_pressure};
  out_data.unit = "Pa";
  out_data.sensorType = jni::SensorType::BAROMETER;
  return true;
}

std::string BarometerSensorController::PrettyName() const {
  return "Barometer Sensor";
}

}  // namespace ros2_android
