#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "lidar/base/lidar_device.h"

namespace ros2_android
{

  /**
   * YDLIDAR device implementation
   * Uses file descriptor from USB Host API for serial communication
   *
   * Phase 4.3: Basic implementation with FD-based serial I/O
   * Future: Integrate full YDLIDAR SDK for protocol handling
   */
  class YDLidarDevice : public LidarDevice
  {
  public:
    /**
     * Create YDLIDAR device
     * @param fd File descriptor from USB Host API
     * @param device_path USB device path (e.g., "/dev/bus/usb/001/002")
     * @param unique_id Unique identifier for this device
     */
    YDLidarDevice(int fd, const std::string &device_path, const std::string &unique_id);
    virtual ~YDLidarDevice();

    // LidarDevice interface
    bool Initialize() override;
    void Shutdown() override;
    bool StartScanning() override;
    void StopScanning() override;
    bool IsScanning() const override { return is_scanning_; }
    const std::string &GetUniqueId() const override { return unique_id_; }
    const std::string &GetDevicePath() const override { return device_path_; }

  private:
    /**
     * Read thread - reads data from file descriptor and emits LaserScanData
     */
    void ReadThread();

    /**
     * Generate test scan data (Phase 4.3 stub)
     * Phase 4.4+ will replace with actual YDLIDAR protocol parsing
     */
    void GenerateTestScan();

    int fd_;
    std::string device_path_;
    std::string unique_id_;

    std::atomic<bool> is_scanning_;
    std::atomic<bool> shutdown_;
    std::thread read_thread_;

    // YDLIDAR-specific parameters (will come from device in full implementation)
    float angle_min_ = -3.14159f;    // -π rad
    float angle_max_ = 3.14159f;     // +π rad
    float range_min_ = 0.1f;         // 0.1m
    float range_max_ = 12.0f;        // 12m (typical for YDLIDAR X4)
    float scan_frequency_ = 10.0f;   // 10 Hz
  };

} // namespace ros2_android
