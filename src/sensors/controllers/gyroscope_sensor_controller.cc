#include "sensors/controllers/gyroscope_sensor_controller.h"

#include <sstream>

#include "core/sensor_data_callback_queue.h"

namespace ros2_android {

GyroscopeSensorController::GyroscopeSensorController(GyroscopeSensor* sensor,
                                                     RosInterface& ros)
    : sensor_(sensor),
      publisher_(ros),
      ros_(ros),
      SensorDataProvider(std::string(sensor->Descriptor().name) +
                         sensor->Descriptor().vendor) {
  sensor->SetListener(std::bind(&GyroscopeSensorController::OnGyroReading, this,
                                std::placeholders::_1));
  std::string topic = "/" + ros.GetDeviceId() + "/sensors/gyroscope";
  publisher_.SetTopic(topic.c_str());
}

void GyroscopeSensorController::OnGyroReading(
    const geometry_msgs::msg::TwistStamped& msg) {
  // Create a copy and update frame_id with device namespace
  geometry_msgs::msg::TwistStamped namespaced_msg = msg;
  namespaced_msg.header.frame_id = ros_.GetDeviceId() + "_gyroscope";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = namespaced_msg;
  }
  publisher_.Publish(namespaced_msg);

  // Trigger callback to notify UI of new sensor data (throttled to 10 Hz)
  ros2_android::PostSensorDataUpdate(std::string(UniqueId()));
}

std::string GyroscopeSensorController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;
  ss << "{\"values\":[" << last_msg_.twist.angular.x << ","
     << last_msg_.twist.angular.y << "," << last_msg_.twist.angular.z
     << "],\"unit\":\"rad/s\"}";
  return ss.str();
}

bool GyroscopeSensorController::GetLastMeasurement(jni::SensorReadingData& out_data) {
  std::lock_guard<std::mutex> lock(mutex_);
  out_data.values = {last_msg_.twist.angular.x, last_msg_.twist.angular.y, last_msg_.twist.angular.z};
  out_data.unit = "rad/s";
  out_data.sensorType = jni::SensorType::GYROSCOPE;
  return true;
}

std::string GyroscopeSensorController::PrettyName() const {
  return "Gyroscope Sensor";
}

}  // namespace ros2_android
