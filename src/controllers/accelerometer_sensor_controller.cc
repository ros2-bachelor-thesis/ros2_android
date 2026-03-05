#include "controllers/accelerometer_sensor_controller.h"

#include <sstream>

namespace sensors_for_ros {

AccelerometerSensorController::AccelerometerSensorController(
    AccelerometerSensor* sensor, RosInterface& ros)
    : sensor_(sensor),
      publisher_(ros),
      SensorDataProvider(std::string(sensor->Descriptor().name) +
                         sensor->Descriptor().vendor) {
  sensor->SetListener(std::bind(&AccelerometerSensorController::OnSensorReading,
                                this, std::placeholders::_1));
  publisher_.SetTopic("accelerometer");
  publisher_.Enable();
}

void AccelerometerSensorController::OnSensorReading(
    const geometry_msgs::msg::AccelStamped& msg) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = msg;
  }
  publisher_.Publish(msg);
}

std::string AccelerometerSensorController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;
  ss << "{\"values\":[" << last_msg_.accel.linear.x << ","
     << last_msg_.accel.linear.y << "," << last_msg_.accel.linear.z
     << "],\"unit\":\"m/s^2\"}";
  return ss.str();
}

std::string AccelerometerSensorController::PrettyName() const {
  return "Accelerometer Sensor";
}

}  // namespace sensors_for_ros
