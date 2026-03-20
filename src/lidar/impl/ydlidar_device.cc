#include "lidar/impl/ydlidar_device.h"

#include <unistd.h>
#include <chrono>
#include <cmath>

#include "core/log.h"
#include "core/notification_queue.h"

namespace ros2_android
{

  YDLidarDevice::YDLidarDevice(int fd, const std::string &device_path, const std::string &unique_id)
      : fd_(fd),
        device_path_(device_path),
        unique_id_(unique_id),
        is_scanning_(false),
        shutdown_(false)
  {
    LOGI("YDLidarDevice created: fd=%d, path=%s, id=%s", fd_, device_path_.c_str(), unique_id_.c_str());
  }

  YDLidarDevice::~YDLidarDevice()
  {
    Shutdown();
    LOGI("YDLidarDevice destroyed: %s", unique_id_.c_str());
  }

  bool YDLidarDevice::Initialize()
  {
    LOGI("Initializing YDLIDAR device: %s", unique_id_.c_str());

    // Phase 4.3: Minimal initialization
    // Future phases will add:
    // - YDLIDAR SDK initialization
    // - Device detection and model identification
    // - Configuration parameter setup
    // - Health check

    if (fd_ < 0)
    {
      LOGE("Invalid file descriptor: %d", fd_);
      PostNotification(NotificationSeverity::ERROR, "LIDAR: Invalid file descriptor");
      return false;
    }

    LOGI("LIDAR device initialized successfully: %s", unique_id_.c_str());
    PostNotification(NotificationSeverity::WARNING, "LIDAR initialized (test mode)");
    return true;
  }

  void YDLidarDevice::Shutdown()
  {
    LOGI("Shutting down YDLIDAR device: %s", unique_id_.c_str());

    StopScanning();

    shutdown_ = true;

    // Wait for read thread to finish
    if (read_thread_.joinable())
    {
      read_thread_.join();
    }

    // Close file descriptor
    if (fd_ >= 0)
    {
      close(fd_);
      fd_ = -1;
    }

    LOGI("LIDAR device shut down: %s", unique_id_.c_str());
  }

  bool YDLidarDevice::StartScanning()
  {
    if (is_scanning_)
    {
      LOGW("LIDAR already scanning: %s", unique_id_.c_str());
      return true;
    }

    LOGI("Starting LIDAR scan: %s", unique_id_.c_str());

    // Phase 4.3: Launch read thread
    // Future phases will add:
    // - Send start scan command to LIDAR
    // - Wait for acknowledgment
    // - Parse actual scan data from serial port

    is_scanning_ = true;
    read_thread_ = std::thread(&YDLidarDevice::ReadThread, this);

    PostNotification(NotificationSeverity::WARNING, "LIDAR scanning started (test mode)");
    LOGI("LIDAR scanning started: %s", unique_id_.c_str());
    return true;
  }

  void YDLidarDevice::StopScanning()
  {
    if (!is_scanning_)
    {
      return;
    }

    LOGI("Stopping LIDAR scan: %s", unique_id_.c_str());

    // Phase 4.3: Set flag to stop read thread
    // Future phases will add:
    // - Send stop scan command to LIDAR
    // - Wait for acknowledgment

    is_scanning_ = false;

    LOGI("LIDAR scanning stopped: %s", unique_id_.c_str());
  }

  void YDLidarDevice::ReadThread()
  {
    LOGI("LIDAR read thread started: %s", unique_id_.c_str());

    // Phase 4.3: Generate test scan data at 10 Hz
    // Future phases will replace this with actual protocol parsing:
    // - Read packet header from FD
    // - Parse packet type (scan data, device info, etc.)
    // - Validate checksum
    // - Extract range/intensity data
    // - Convert to LaserScanData format

    while (is_scanning_ && !shutdown_)
    {
      GenerateTestScan();

      // Sleep to maintain scan rate (100ms = 10 Hz)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOGI("LIDAR read thread stopped: %s", unique_id_.c_str());
  }

  void YDLidarDevice::GenerateTestScan()
  {
    // Generate synthetic scan data for testing
    // This will be replaced with actual YDLIDAR protocol parsing

    LaserScanData scan;

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    scan.timestamp_ns = ns.count();

    // Scan parameters
    scan.angle_min = angle_min_;
    scan.angle_max = angle_max_;
    scan.range_min = range_min_;
    scan.range_max = range_max_;
    scan.scan_time = 1.0f / scan_frequency_; // Time for full 360° scan

    // Generate 360 points (1° resolution)
    const int num_points = 360;
    scan.angle_increment = (angle_max_ - angle_min_) / (num_points - 1);
    scan.time_increment = scan.scan_time / num_points;

    scan.ranges.resize(num_points);
    scan.intensities.resize(num_points);

    // Generate simple circular pattern for testing
    // Real data will come from LIDAR hardware
    for (int i = 0; i < num_points; ++i)
    {
      float angle = angle_min_ + i * scan.angle_increment;

      // Simple pattern: distance varies sinusoidally
      float base_range = 2.0f + 1.0f * std::sin(angle * 2.0f);
      scan.ranges[i] = base_range;
      scan.intensities[i] = 100.0f; // Arbitrary intensity value
    }

    // Emit the scan data (triggers OnLaserScan in LidarController)
    Emit(scan);
  }

} // namespace ros2_android
