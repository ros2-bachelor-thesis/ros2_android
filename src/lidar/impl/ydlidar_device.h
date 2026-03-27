#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "lidar/base/lidar_device.h"

// Forward declare YDLIDAR SDK class to avoid header pollution
class CYdLidar;

namespace ros2_android
{

  /**
   * YDLIDAR device implementation using YDLIDAR SDK via USB Serial (no root required)
   * Model-agnostic: SDK auto-detects LIDAR model and configures parameters
   *
   * Supports all YDLIDAR models through SDK:
   * - TG series (TG15, TG30, TG50) - TOF lidars
   * - Triangle series (X2, X4, G4, G6, etc.)
   * - Other series (GS, T, SDM, etc.)
   *
   * Requirements:
   * - Android USB Host API support
   * - USB serial driver (CP210x, CH340, FTDI, etc.)
   * - File descriptor passed via JNI from Java USB Serial library
   */
  class YDLidarDevice : public LidarDevice
  {
  public:
    /**
     * Create YDLIDAR device with USB device path
     * @param usb_path USB device path (e.g., "/dev/bus/usb/001/002")
     * @param unique_id Unique identifier for this device
     * @param baudrate Serial baudrate (e.g., 115200, 230400, 460800, 512000)
     */
    YDLidarDevice(const std::string &usb_path, const std::string &unique_id, int baudrate);
    virtual ~YDLidarDevice();

    // LidarDevice interface
    bool Initialize() override;
    void Shutdown() override;
    bool StartScanning() override;
    void StopScanning() override;
    bool IsScanning() const override { return is_scanning_; }
    const std::string &GetUniqueId() const override { return unique_id_; }
    const std::string &GetDevicePath() const override { return usb_path_; }

  private:
    /**
     * Read thread - polls SDK for scan data and emits LaserScanData
     */
    void ReadThread();

    /**
     * Convert SDK LaserScan to our LaserScanData format
     */
    void ConvertScan(const void *sdk_scan);

    std::string usb_path_;   // USB device path (e.g., "/dev/bus/usb/001/002")
    std::string unique_id_;
    int baudrate_;           // Serial baudrate

    std::atomic<bool> is_scanning_;
    std::atomic<bool> shutdown_;
    std::thread read_thread_;

    // YDLIDAR SDK instance
    std::unique_ptr<CYdLidar> lidar_;
  };

} // namespace ros2_android
