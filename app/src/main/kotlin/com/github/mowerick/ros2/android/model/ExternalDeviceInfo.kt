package com.github.mowerick.ros2.android.model

data class ExternalDeviceInfo(
    val uniqueId: String,           // "ydlidar_tg15_001002"
    val name: String,                // "YDLIDAR TG15"
    val deviceType: ExternalDeviceType,
    val usbPath: String,             // "/dev/bus/usb/001/002"
    val vendorId: Int,
    val productId: Int,
    val topicName: String,           // "/scan"
    val topicType: String,           // "sensor_msgs/msg/LaserScan"
    val connected: Boolean,
    val enabled: Boolean
)

enum class ExternalDeviceType {
    LIDAR,
    USB_CAMERA
}
