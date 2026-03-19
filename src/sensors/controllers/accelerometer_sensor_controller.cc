#include "sensors/controllers/accelerometer_sensor_controller.h"

#include <sstream>

#include "core/sensor_data_callback_queue.h"

namespace ros2_android
{

  AccelerometerSensorController::AccelerometerSensorController(
      AccelerometerSensor *sensor, RosInterface &ros)
      : sensor_(sensor),
        publisher_(ros),
        ros_(ros),
        SensorDataProvider(std::string(sensor->Descriptor().name) +
                           sensor->Descriptor().vendor)
  {
    sensor->SetListener(std::bind(&AccelerometerSensorController::OnSensorReading,
                                  this, std::placeholders::_1));
    std::string topic = "/" + ros.GetDeviceId() + "/sensors/accelerometer";
    publisher_.SetTopic(topic.c_str());
  }

  void AccelerometerSensorController::OnSensorReading(
      const geometry_msgs::msg::AccelStamped &msg)
  {
    // Create a copy and update frame_id with device namespace
    geometry_msgs::msg::AccelStamped namespaced_msg = msg;
    namespaced_msg.header.frame_id = ros_.GetDeviceId() + "_accelerometer";

    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_msg_ = namespaced_msg;
    }
    publisher_.Publish(namespaced_msg);

    // Trigger callback to notify UI of new sensor data (throttled to 10 Hz)
    ros2_android::PostSensorDataUpdate(std::string(UniqueId()));
  }

  std::string AccelerometerSensorController::GetLastMeasurementJson()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "{\"values\":[" << last_msg_.accel.linear.x << ","
       << last_msg_.accel.linear.y << "," << last_msg_.accel.linear.z
       << "],\"unit\":\"m/s\u00B2\"}";
    return ss.str();
  }

  bool AccelerometerSensorController::GetLastMeasurement(jni::SensorReadingData &out_data)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    out_data.values = {last_msg_.accel.linear.x, last_msg_.accel.linear.y, last_msg_.accel.linear.z};
    out_data.unit = "m/s\u00B2";
    out_data.sensorType = jni::SensorType::ACCELEROMETER;
    return true;
  }

  std::string AccelerometerSensorController::PrettyName() const
  {
    return "Accelerometer Sensor";
  }

} // namespace ros2_android
