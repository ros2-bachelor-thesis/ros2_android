#include "sensors/controllers/magnetometer_sensor_controller.h"

#include <sstream>

#include "core/sensor_data_callback_queue.h"

namespace ros2_android {

MagnetometerSensorController::MagnetometerSensorController(
    MagnetometerSensor* sensor, RosInterface& ros)
    : sensor_(sensor),
      publisher_(ros),
      ros_(ros),
      SensorDataProvider(std::string(sensor->Descriptor().name) +
                         sensor->Descriptor().vendor) {
  sensor->SetListener(std::bind(&MagnetometerSensorController::OnSensorReading,
                                this, std::placeholders::_1));
  std::string topic = "/" + ros.GetDeviceId() + "/sensors/magnetometer";
  publisher_.SetTopic(topic.c_str());
}

void MagnetometerSensorController::OnSensorReading(
    const sensor_msgs::msg::MagneticField& msg) {
  // Create a copy and update frame_id with device namespace
  sensor_msgs::msg::MagneticField namespaced_msg = msg;
  namespaced_msg.header.frame_id = ros_.GetDeviceId() + "_magnetometer";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = namespaced_msg;
  }
  publisher_.Publish(namespaced_msg);

  // Trigger callback to notify UI of new sensor data (throttled to 10 Hz)
  ros2_android::PostSensorDataUpdate(std::string(UniqueId()));
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

bool MagnetometerSensorController::GetLastMeasurement(jni::SensorReadingData& out_data) {
  std::lock_guard<std::mutex> lock(mutex_);
  out_data.values = {
    last_msg_.magnetic_field.x * kMicroTeslaPerTesla,
    last_msg_.magnetic_field.y * kMicroTeslaPerTesla,
    last_msg_.magnetic_field.z * kMicroTeslaPerTesla
  };
  out_data.unit = "uT";
  out_data.sensorType = jni::SensorType::MAGNETOMETER;
  return true;
}

std::string MagnetometerSensorController::PrettyName() const {
  return "Magnetometer Sensor";
}

}  // namespace ros2_android
