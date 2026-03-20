#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <sensor_msgs/msg/laser_scan.hpp>

#include "lidar/base/lidar_device.h"
#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"

namespace ros2_android
{

  /**
   * LIDAR controller - manages LIDAR device and publishes LaserScan messages
   * Follows the same pattern as CameraController
   */
  class LidarController : public SensorDataProvider
  {
  public:
    /**
     * Create a LIDAR controller
     * @param device LIDAR device instance (ownership transferred)
     * @param ros ROS interface for publishing
     */
    LidarController(std::unique_ptr<LidarDevice> device, RosInterface &ros);
    virtual ~LidarController();

    // SensorDataProvider interface
    std::string PrettyName() const override;
    std::string GetLastMeasurementJson() override;
    bool GetLastMeasurement(jni::SensorReadingData &out_data) override;

    void Enable() override;
    void Disable() override;
    bool IsEnabled() const override { return scan_pub_.Enabled(); }

    // LIDAR-specific accessors
    const char *TopicName() const override { return scan_pub_.Topic(); }
    const char *TopicType() const override { return scan_pub_.Type(); }
    std::string GetUniqueId() const;
    std::string GetDevicePath() const;

  protected:
    /**
     * Called when new LaserScan data is available from device
     */
    void OnLaserScan(const LaserScanData &scan_data);

  private:
    std::unique_ptr<LidarDevice> device_;
    RosInterface &ros_;
    Publisher<sensor_msgs::msg::LaserScan> scan_pub_;

    std::mutex scan_mutex_;
    LaserScanData last_scan_; // Last scan data for GetLastMeasurement
  };

} // namespace ros2_android
