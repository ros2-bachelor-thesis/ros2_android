#include "lidar/controllers/lidar_controller.h"

#include <sstream>

#include "core/log.h"

namespace ros2_android
{

  LidarController::LidarController(std::unique_ptr<LidarDevice> device, RosInterface &ros)
      : SensorDataProvider(device ? device->GetUniqueId() : ""),
        device_(std::move(device)),
        ros_(ros),
        scan_pub_(ros)
  {
    if (!device_)
    {
      LOGE("LidarController: device is null");
      return;
    }

    LOGI("LidarController created for device: %s", device_->GetUniqueId().c_str());

    // Set topic with device ID prefix
    std::string topic = "/" + ros.GetDeviceId() + "/scan";
    scan_pub_.SetTopic(topic.c_str());

    // Use BEST_EFFORT QoS for LaserScan (standard for sensor streaming)
    auto qos = rclcpp::QoS(rclcpp::KeepLast(1))
                   .best_effort()
                   .durability_volatile();
    scan_pub_.SetQos(qos);

    // Subscribe to LaserScan events from device
    device_->SetListener([this](const LaserScanData &scan_data)
                         { OnLaserScan(scan_data); });
  }

  LidarController::~LidarController()
  {
    if (device_)
    {
      device_->Shutdown();
    }
    LOGI("LidarController destroyed");
  }

  std::string LidarController::PrettyName() const
  {
    if (!device_)
      return "LIDAR (disconnected)";
    return "LIDAR " + device_->GetUniqueId();
  }

  std::string LidarController::GetLastMeasurementJson()
  {
    std::lock_guard<std::mutex> lock(scan_mutex_);

    if (last_scan_.ranges.empty())
    {
      return "{\"error\": \"No scan data available\"}";
    }

    std::ostringstream ss;
    ss << "{";
    ss << "\"timestamp_ns\": " << last_scan_.timestamp_ns << ",";
    ss << "\"angle_min\": " << last_scan_.angle_min << ",";
    ss << "\"angle_max\": " << last_scan_.angle_max << ",";
    ss << "\"range_min\": " << last_scan_.range_min << ",";
    ss << "\"range_max\": " << last_scan_.range_max << ",";
    ss << "\"num_points\": " << last_scan_.ranges.size();
    ss << "}";

    return ss.str();
  }

  bool LidarController::GetLastMeasurement(jni::SensorReadingData &out_data)
  {
    std::lock_guard<std::mutex> lock(scan_mutex_);

    if (last_scan_.ranges.empty())
    {
      return false;
    }

    // Return summary statistics as sensor reading
    out_data.values.clear();
    out_data.values.push_back(last_scan_.ranges.size()); // Number of points
    out_data.values.push_back(last_scan_.angle_min);
    out_data.values.push_back(last_scan_.angle_max);
    out_data.values.push_back(last_scan_.range_min);
    out_data.values.push_back(last_scan_.range_max);
    out_data.unit = "LaserScan";
    out_data.sensorType = jni::SensorType::UNKNOWN;

    return true;
  }

  void LidarController::Enable()
  {
    if (!device_)
    {
      LOGE("LidarController::Enable: device is null");
      return;
    }

    if (!device_->Initialize())
    {
      LOGE("Failed to initialize LIDAR device");
      return;
    }

    if (!device_->StartScanning())
    {
      LOGE("Failed to start LIDAR scanning");
      return;
    }

    scan_pub_.Enable();
    LOGI("LIDAR publishing enabled on topic: %s", scan_pub_.Topic());
  }

  void LidarController::Disable()
  {
    if (!device_)
    {
      LOGE("LidarController::Disable: device is null");
      return;
    }

    device_->StopScanning();
    scan_pub_.Disable();
    LOGI("LIDAR publishing disabled");
  }

  std::string LidarController::GetUniqueId() const
  {
    if (!device_)
      return "";
    return device_->GetUniqueId();
  }

  std::string LidarController::GetDevicePath() const
  {
    if (!device_)
      return "";
    return device_->GetDevicePath();
  }

  void LidarController::OnLaserScan(const LaserScanData &scan_data)
  {
    // Store last scan for GetLastMeasurement
    {
      std::lock_guard<std::mutex> lock(scan_mutex_);
      last_scan_ = scan_data;
    }

    // Convert to ROS LaserScan message and publish
    auto msg = std::make_unique<sensor_msgs::msg::LaserScan>();

    // Header
    msg->header.stamp.sec = scan_data.timestamp_ns / 1000000000ULL;
    msg->header.stamp.nanosec = scan_data.timestamp_ns % 1000000000ULL;
    msg->header.frame_id = "laser_frame"; // TODO: make configurable

    // Scan parameters
    msg->angle_min = scan_data.angle_min;
    msg->angle_max = scan_data.angle_max;
    msg->angle_increment = scan_data.angle_increment;
    msg->time_increment = scan_data.time_increment;
    msg->scan_time = scan_data.scan_time;
    msg->range_min = scan_data.range_min;
    msg->range_max = scan_data.range_max;

    // Data
    msg->ranges = scan_data.ranges;
    msg->intensities = scan_data.intensities;

    // Publish
    scan_pub_.Publish(std::move(msg));
  }

} // namespace ros2_android
