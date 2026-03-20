#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/events.h"
#include "core/log.h"

namespace ros2_android
{
  /**
   * LaserScan data structure matching sensor_msgs/msg/LaserScan
   */
  struct LaserScanData
  {
    uint64_t timestamp_ns;        // Timestamp in nanoseconds
    float angle_min;              // Start angle of scan (rad)
    float angle_max;              // End angle of scan (rad)
    float angle_increment;        // Angular distance between measurements (rad)
    float time_increment;         // Time between measurements (sec)
    float scan_time;              // Time between scans (sec)
    float range_min;              // Minimum range value (m)
    float range_max;              // Maximum range value (m)
    std::vector<float> ranges;    // Range data (m)
    std::vector<float> intensities; // Intensity data
  };

  /**
   * Base class for LIDAR devices
   * Emits LaserScanData events when new scan data is available
   */
  class LidarDevice : public event::Emitter<LaserScanData>
  {
  public:
    virtual ~LidarDevice() = default;

    /**
     * Initialize the LIDAR device
     * @return true on success, false on failure
     */
    virtual bool Initialize() = 0;

    /**
     * Shutdown the LIDAR device and release resources
     */
    virtual void Shutdown() = 0;

    /**
     * Start scanning (begin emitting LaserScanData events)
     * @return true on success, false on failure
     */
    virtual bool StartScanning() = 0;

    /**
     * Stop scanning (stop emitting events)
     */
    virtual void StopScanning() = 0;

    /**
     * Check if device is currently scanning
     */
    virtual bool IsScanning() const = 0;

    /**
     * Get device unique identifier
     */
    virtual const std::string& GetUniqueId() const = 0;

    /**
     * Get device path (e.g., "/dev/bus/usb/001/002")
     */
    virtual const std::string& GetDevicePath() const = 0;
  };

} // namespace ros2_android
