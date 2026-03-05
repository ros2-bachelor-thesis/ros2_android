#include "controllers/gyroscope_sensor_controller.h"

#include <sstream>

namespace ros2_android {

GyroscopeSensorController::GyroscopeSensorController(GyroscopeSensor* sensor,
                                                     RosInterface& ros)
    : sensor_(sensor),
      publisher_(ros),
      SensorDataProvider(std::string(sensor->Descriptor().name) +
                         sensor->Descriptor().vendor) {
  sensor->SetListener(std::bind(&GyroscopeSensorController::OnGyroReading, this,
                                std::placeholders::_1));
  publisher_.SetTopic("gyroscope");
}

void GyroscopeSensorController::OnGyroReading(
    const geometry_msgs::msg::TwistStamped& msg) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = msg;
  }
  publisher_.Publish(msg);
}

std::string GyroscopeSensorController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;
  ss << "{\"values\":[" << last_msg_.twist.angular.x << ","
     << last_msg_.twist.angular.y << "," << last_msg_.twist.angular.z
     << "],\"unit\":\"rad/s\"}";
  return ss.str();
}

std::string GyroscopeSensorController::PrettyName() const {
  return "Gyroscope Sensor";
}

}  // namespace ros2_android
